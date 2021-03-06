#include "flightros/pilot/flight_pilot.hpp"

#define use_multi true
#define CAMERA_RES_WIDTH 360 //180 //720
#define CAMERA_RES_HEIGHT 240 //120 //480
#define CAMERA_FOV 90
#define DRONE2CAM_OFFSET 0.5
namespace flightros {

FlightPilot::FlightPilot(const ros::NodeHandle &nh, const ros::NodeHandle &pnh)
  : nh_(nh),
    pnh_(pnh),
    scene_id_(UnityScene::WAREHOUSE),
    unity_ready_(false),
    unity_render_(false),
    receive_id_(0),
    main_loop_freq_(30.0),
    main_render_freq_(30.0) {
  // load parameters
  if (!loadParams()) {
    ROS_WARN("[%s] Could not load all parameters.",
             pnh_.getNamespace().c_str());
  } else {
    ROS_INFO("[%s] Loaded all parameters.", pnh_.getNamespace().c_str());
  }

  // quad shared info
  Vector<3> B_r_BC(0, DRONE2CAM_OFFSET, 0);
  Matrix<3, 3> R_BC = Quaternion(1.0, 0.0, 0.0, 0.0).toRotationMatrix();
  std::cout << R_BC << std::endl;


  // ============================== drone 1 ==============================
  // quad initialization
  quad_ptr_1_ = std::make_shared<Quadrotor>();

  // add mono camera
  rgb_camera_1_ = std::make_shared<RGBCamera>();
  rgb_camera_1_->setFOV(CAMERA_FOV);
  rgb_camera_1_->setWidth(CAMERA_RES_WIDTH);
  rgb_camera_1_->setHeight(CAMERA_RES_HEIGHT);
  rgb_camera_1_->setRelPose(B_r_BC, R_BC);
  rgb_camera_1_->setPostProcesscing(
    std::vector<bool>{true, false, false});  // depth, segmentation, optical flow
  quad_ptr_1_->addRGBCamera(rgb_camera_1_);

  // initialization
  quad_state_1_.setZero();
  quad_ptr_1_->reset(quad_state_1_);


  // ============================== drone 2 ==============================
  // quad initialization
  quad_ptr_2_ = std::make_shared<Quadrotor>();

  // add mono camera
  rgb_camera_2_ = std::make_shared<RGBCamera>();
  rgb_camera_2_->setFOV(CAMERA_FOV);
  rgb_camera_2_->setWidth(CAMERA_RES_WIDTH);
  rgb_camera_2_->setHeight(CAMERA_RES_HEIGHT);
  rgb_camera_2_->setRelPose(B_r_BC, R_BC);
  rgb_camera_2_->setPostProcesscing(
    std::vector<bool>{true, false, false});  // depth, segmentation, optical flow
  quad_ptr_2_->addRGBCamera(rgb_camera_2_);

  // initialization
  quad_state_2_.setZero();
  quad_ptr_2_->reset(quad_state_2_);



  // ============================== drone 0 ==============================
  // quad initialization
  quad_ptr_ = std::make_shared<Quadrotor>();

  // add mono camera
  rgb_camera_ = std::make_shared<RGBCamera>();
  rgb_camera_->setFOV(CAMERA_FOV);
  rgb_camera_->setWidth(CAMERA_RES_WIDTH);
  rgb_camera_->setHeight(CAMERA_RES_HEIGHT);
  rgb_camera_->setRelPose(B_r_BC, R_BC);
  rgb_camera_->setPostProcesscing(
    std::vector<bool>{true, false, false});  // depth, segmentation, optical flow
  quad_ptr_->addRGBCamera(rgb_camera_);

  // initialization
  quad_state_.setZero();
  quad_ptr_->reset(quad_state_);



  // ============================== subscribe and publish ==============================

  // initialize publisher
  image_transport::ImageTransport it(pnh);

  //publisher
  rgb_pub_ = it.advertise("/hummingbird0/camera/rgb",1);
//  depth_pub_ = it.advertise("/hummingbird0/camera/depth",1);
  camera_info_pub = nh_.advertise<sensor_msgs::CameraInfo>("/hummingbird0/camera/camera_info",1);

  rgb_pub_1_ = it.advertise("/hummingbird1/camera/rgb",1);
  camera_info_pub_1 = nh_.advertise<sensor_msgs::CameraInfo>("/hummingbird1/camera/camera_info",1);

  rgb_pub_2_ = it.advertise("/hummingbird2/camera/rgb",1);
  camera_info_pub_2 = nh_.advertise<sensor_msgs::CameraInfo>("/hummingbird2/camera/camera_info",1);

  //bounding box overlay RGB img
  rgb_bounding_box_pub_ = it.advertise("/hummingbird0/camera/bounding_box",1);
  rgb_bounding_box_pub_one = it.advertise("/hummingbird1/camera/bounding_box",1);
  rgb_bounding_box_pub_two = it.advertise("/hummingbird2/camera/bounding_box",1);

  //bounding box with 2D position
  track_bounding_box_pub_zero = nh_.advertise<geometry_msgs::PoseArray>("/hummingbird0/track/bounding_box",1);
  track_bounding_box_pub_one = nh_.advertise<geometry_msgs::PoseArray>("/hummingbird1/track/bounding_box",1);
  track_bounding_box_pub_two = nh_.advertise<geometry_msgs::PoseArray>("/hummingbird2/track/bounding_box",1);



  init_camera_info();

  // initialize subscriber call backs
  sub_state_est_ = nh_.subscribe("flight_pilot/state_estimate0", 1, &FlightPilot::poseCallback, this);

#ifdef use_multi
  sub_state_est_1 = nh_.subscribe("flight_pilot/state_estimate1", 1,&FlightPilot::poseCallback_1, this);
  sub_state_est_2 = nh_.subscribe("flight_pilot/state_estimate2", 1,&FlightPilot::poseCallback_2, this);


#endif

  timer_main_loop_ = nh_.createTimer(ros::Rate(main_loop_freq_), &FlightPilot::mainLoopCallback, this);
  timer_render_loop_ = nh_.createTimer(ros::Rate(main_render_freq_), &FlightPilot::mainRenderCallback, this);


  // wait until the gazebo and unity are loaded
  ros::Duration(5.0).sleep();

  // connect unity
  setUnity(unity_render_);
  connectUnity();
}

FlightPilot::~FlightPilot() {}

void FlightPilot::init_camera_info() {
//     [fx  0 cx]
// K = [ 0 fy cy]
//     [ 0  0  1]
  float f = ( float(CAMERA_RES_HEIGHT/2) / float(tan((M_PI*CAMERA_FOV/180)/2)) );
  float fx = f;
  float fy = f;
  float cx = CAMERA_RES_WIDTH/2;
  float cy = CAMERA_RES_HEIGHT/2;


  camera_info_msg.header.frame_id = "camera";
  camera_info_msg.distortion_model = "plumb_bob";
  camera_info_msg.width = CAMERA_RES_WIDTH;
  camera_info_msg.height = CAMERA_RES_HEIGHT;
  camera_info_msg.K = {fx, 0, cx, 0, fy, cy, 0, 0, 1};

}

void FlightPilot::poseCallback(const nav_msgs::Odometry::ConstPtr &msg) {
  quad_state_.x[QS::POSX] = (Scalar)msg->pose.pose.position.y * -1;
  quad_state_.x[QS::POSY] = (Scalar)msg->pose.pose.position.x;
  quad_state_.x[QS::POSZ] = (Scalar)msg->pose.pose.position.z;
  quad_state_.x[QS::ATTW] = (Scalar)msg->pose.pose.orientation.w;
  quad_state_.x[QS::ATTX] = (Scalar)msg->pose.pose.orientation.y * -1;
  quad_state_.x[QS::ATTY] = (Scalar)msg->pose.pose.orientation.x;
  quad_state_.x[QS::ATTZ] = (Scalar)msg->pose.pose.orientation.z;

  //rotate FlightPilot Render +90deg ZAxis to match Gazebo

}

void FlightPilot::poseCallback_1(const nav_msgs::Odometry::ConstPtr &msg) {
#ifdef use_multi
  quad_state_1_.x[QS::POSX] = (Scalar)msg->pose.pose.position.y * -1;
  quad_state_1_.x[QS::POSY] = (Scalar)msg->pose.pose.position.x;
  quad_state_1_.x[QS::POSZ] = (Scalar)msg->pose.pose.position.z;
  quad_state_1_.x[QS::ATTW] = (Scalar)msg->pose.pose.orientation.w;
  quad_state_1_.x[QS::ATTX] = (Scalar)msg->pose.pose.orientation.y * -1;
  quad_state_1_.x[QS::ATTY] = (Scalar)msg->pose.pose.orientation.x;
  quad_state_1_.x[QS::ATTZ] = (Scalar)msg->pose.pose.orientation.z;

  //rotate FlightPilot Render +90deg ZAxis to match Gazebo

#endif

}

void FlightPilot::poseCallback_2(const nav_msgs::Odometry::ConstPtr &msg) {
#ifdef use_multi
  quad_state_2_.x[QS::POSX] = (Scalar)msg->pose.pose.position.y * -1;
  quad_state_2_.x[QS::POSY] = (Scalar)msg->pose.pose.position.x;
  quad_state_2_.x[QS::POSZ] = (Scalar)msg->pose.pose.position.z;
  quad_state_2_.x[QS::ATTW] = (Scalar)msg->pose.pose.orientation.w;
  quad_state_2_.x[QS::ATTX] = (Scalar)msg->pose.pose.orientation.y * -1;
  quad_state_2_.x[QS::ATTY] = (Scalar)msg->pose.pose.orientation.x;
  quad_state_2_.x[QS::ATTZ] = (Scalar)msg->pose.pose.orientation.z;

  //rotate FlightPilot Render +90deg ZAxis to match Gazebo

#endif

}

void FlightPilot::poseCallback_3(const nav_msgs::Odometry::ConstPtr &msg) {
#ifdef use_multi
  quad_state_3_.x[QS::POSX] = (Scalar)msg->pose.pose.position.y * -1;
  quad_state_3_.x[QS::POSY] = (Scalar)msg->pose.pose.position.x;
  quad_state_3_.x[QS::POSZ] = (Scalar)msg->pose.pose.position.z;
  quad_state_3_.x[QS::ATTW] = (Scalar)msg->pose.pose.orientation.w;
  quad_state_3_.x[QS::ATTX] = (Scalar)msg->pose.pose.orientation.y * -1;
  quad_state_3_.x[QS::ATTY] = (Scalar)msg->pose.pose.orientation.x;
  quad_state_3_.x[QS::ATTZ] = (Scalar)msg->pose.pose.orientation.z;

  //rotate FlightPilot Render +90deg ZAxis to match Gazebo

#endif

}

void FlightPilot::poseCallback_4(const nav_msgs::Odometry::ConstPtr &msg) {
#ifdef use_multi
  quad_state_4_.x[QS::POSX] = (Scalar)msg->pose.pose.position.y * -1;
  quad_state_4_.x[QS::POSY] = (Scalar)msg->pose.pose.position.x;
  quad_state_4_.x[QS::POSZ] = (Scalar)msg->pose.pose.position.z;
  quad_state_4_.x[QS::ATTW] = (Scalar)msg->pose.pose.orientation.w;
  quad_state_4_.x[QS::ATTX] = (Scalar)msg->pose.pose.orientation.y * -1;
  quad_state_4_.x[QS::ATTY] = (Scalar)msg->pose.pose.orientation.x;
  quad_state_4_.x[QS::ATTZ] = (Scalar)msg->pose.pose.orientation.z;

  //rotate FlightPilot Render +90deg ZAxis to match Gazebo

#endif

}


void FlightPilot::mainRenderCallback(const ros::TimerEvent &event) {

#ifdef use_multi
  quad_ptr_1_->setState(quad_state_1_);
  quad_ptr_2_->setState(quad_state_2_);

#endif

  //Get next render with updated quad State
  quad_ptr_->setState(quad_state_);

  if (unity_render_ && unity_ready_) {
    unity_bridge_ptr_->getRender(0);
    unity_bridge_ptr_->handleOutput();

  }
}

//return point from TF
geometry_msgs::Point FlightPilot::getPose_from_tf(const tf::StampedTransform &tf_msg) {

  geometry_msgs::Point temp_point;
  temp_point.x = tf_msg.getOrigin().x();
  temp_point.y = tf_msg.getOrigin().y();
  temp_point.z = tf_msg.getOrigin().z();
  //ROS_INFO("TF12 X: %f, Y: %f, Z: %f\n", temp_point.x , temp_point.y , temp_point.z );

  return temp_point;
}

//return 2D point from 3D projection
geometry_msgs::Point FlightPilot::project_2d_from_3d(const geometry_msgs::Point &point_msg) {

  geometry_msgs::Point projected_point;

  //default init
  projected_point.x = CAMERA_RES_WIDTH/2;
  projected_point.y = CAMERA_RES_HEIGHT/2;

  //project 3D to 2D
  //px = (-1) *fy*y/x + cx (row)
  //py = (-1) *fx*z/x + cy (col)
  projected_point.x = -1*camera_info_msg.K[4]*point_msg.y/point_msg.x + camera_info_msg.K[2];
  projected_point.y = -1*camera_info_msg.K[0]*point_msg.z/point_msg.x + camera_info_msg.K[5];

  //ROS_INFO("px12 pX: %d, pY: %d\n", (int)projected_point.x, (int)projected_point.y);

  return projected_point;
}


void FlightPilot::mainLoopCallback(const ros::TimerEvent &event) {

  //add Image Data Retrieve
  cv::Mat img;
  cv::Mat img_depth;
  cv::Mat img_one;
  cv::Mat img_two;
  cv::Mat img_depth_two;
  cv::Mat img_three;
  cv::Mat img_four;
  cv::Mat img_depth_three;
  cv::Mat img_depth_four;
  cv::Mat img_bounding_box;
  cv::Mat img_bounding_box_one;
  cv::Mat img_bounding_box_two;

  camera_timestamp = ros::Time::now();

  //0th camera
  rgb_camera_->getRGBImage(img);
  sensor_msgs::ImagePtr rgb_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img).toImageMsg();
  rgb_msg->header.stamp = camera_timestamp;
  rgb_pub_.publish(rgb_msg);

//  rgb_camera_->getDepthMap(img_depth);
//  sensor_msgs::ImagePtr depth_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img_depth).toImageMsg();
//  depth_msg->header.stamp = camera_timestamp;
//  depth_pub_.publish(depth_msg);

//#ifdef use_multi
  // 1st camera
  rgb_camera_1_->getRGBImage(img_one);
  sensor_msgs::ImagePtr rgb_msg_one = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img_one).toImageMsg();
  rgb_msg_one->header.stamp = camera_timestamp;
  rgb_pub_1_.publish(rgb_msg_one);


//  rgb_camera_1_->getDepthMap(img_two);
//  sensor_msgs::ImagePtr depth_msg_two = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img_two).toImageMsg();
//  depth_msg_two->header.stamp = camera_timestamp;
//  depth_pub_1_.publish(depth_msg_two);

  // 2nd camera
    rgb_camera_2_->getRGBImage(img_two);
    sensor_msgs::ImagePtr rgb_msg_two = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img_two).toImageMsg();
    rgb_msg_two->header.stamp = camera_timestamp;
    rgb_pub_2_.publish(rgb_msg_two);

//#endif

  //publish camera_Info
  camera_info_msg.header.stamp = camera_timestamp;
  camera_info_pub.publish(camera_info_msg);
  camera_info_pub_1.publish(camera_info_msg);
  camera_info_pub_2.publish(camera_info_msg);


  //================ project 3D into 2D ================

  //lookup TF1,2

  try {
    tf_listener.lookupTransform("/hummingbird0/base_link_cam","/hummingbird1/base_link",ros::Time(0), tf_transform_relative_0_1);
    tf_listener.lookupTransform("/hummingbird0/base_link_cam","/hummingbird2/base_link",ros::Time(0), tf_transform_relative_0_2);

    tf_listener.lookupTransform("/hummingbird1/base_link_cam","/hummingbird0/base_link",ros::Time(0), tf_transform_relative_1_0);
    tf_listener.lookupTransform("/hummingbird1/base_link_cam","/hummingbird2/base_link",ros::Time(0), tf_transform_relative_1_2);

    tf_listener.lookupTransform("/hummingbird2/base_link_cam","/hummingbird0/base_link",ros::Time(0), tf_transform_relative_2_0);
    tf_listener.lookupTransform("/hummingbird2/base_link_cam","/hummingbird1/base_link",ros::Time(0), tf_transform_relative_2_1);


  } catch (tf::TransformException ex){
    //ROS_WARN("%s",ex.what());
}

  //get T {x,y,z}
  geometry_msgs::Point transpose_0_1, transpose_0_2, transpose_1_0, transpose_1_2, transpose_2_0, transpose_2_1;
  transpose_0_1 = getPose_from_tf(tf_transform_relative_0_1);
  transpose_0_2 = getPose_from_tf(tf_transform_relative_0_2);

  transpose_1_0 = getPose_from_tf(tf_transform_relative_1_0);
  transpose_1_2 = getPose_from_tf(tf_transform_relative_1_2);

  transpose_2_0 = getPose_from_tf(tf_transform_relative_2_0);
  transpose_2_1 = getPose_from_tf(tf_transform_relative_2_1);


  //project 3D pose into 2D img coordinate
  geometry_msgs::Point projected_0_1, projected_0_2, projected_1_0, projected_1_2, projected_2_0, projected_2_1 ;
  projected_0_1 = project_2d_from_3d(transpose_0_1);
  projected_0_2 = project_2d_from_3d(transpose_0_2);

  projected_1_0 = project_2d_from_3d(transpose_1_0);
  projected_1_2 = project_2d_from_3d(transpose_1_2);

  projected_2_0 = project_2d_from_3d(transpose_2_0);
  projected_2_1 = project_2d_from_3d(transpose_2_1);



  int line_thickness = 2;
  int circle_radius = 4;//8


  //draw bounding box
  img_bounding_box = img.clone();
  img_bounding_box_one = img_one.clone();
  img_bounding_box_two = img_two.clone();


  //draw green circle
  cv::circle(img_bounding_box, cv::Point(projected_0_1.x,projected_0_1.y), circle_radius, cv::Scalar(0,255,0),line_thickness,8,0);
  cv::circle(img_bounding_box, cv::Point(projected_0_2.x,projected_0_2.y), circle_radius, cv::Scalar(0,255,0),line_thickness,8,0);

  cv::circle(img_bounding_box_one, cv::Point(projected_1_0.x,projected_1_0.y), circle_radius, cv::Scalar(0,255,0),line_thickness,8,0);
  cv::circle(img_bounding_box_one, cv::Point(projected_1_2.x,projected_1_2.y), circle_radius, cv::Scalar(0,255,0),line_thickness,8,0);


  cv::circle(img_bounding_box_two, cv::Point(projected_2_0.x,projected_2_0.y), circle_radius, cv::Scalar(0,255,0),line_thickness,8,0);
  cv::circle(img_bounding_box_two, cv::Point(projected_2_1.x,projected_2_1.y), circle_radius, cv::Scalar(0,255,0),line_thickness,8,0);


  //publish bounding box
  sensor_msgs::ImagePtr box_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img_bounding_box).toImageMsg();
  box_msg->header.stamp = camera_timestamp;
  rgb_bounding_box_pub_.publish(box_msg);

  sensor_msgs::ImagePtr box_msg_one = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img_bounding_box_one).toImageMsg();
  box_msg_one->header.stamp = camera_timestamp;
  rgb_bounding_box_pub_one.publish(box_msg_one);

  sensor_msgs::ImagePtr box_msg_two = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img_bounding_box_two).toImageMsg();
  box_msg_two->header.stamp = camera_timestamp;
  rgb_bounding_box_pub_two.publish(box_msg_two);


//=====================================

  //empty previous frame data
  while (!bbox_pose_array_zero.poses.empty())
     {
     bbox_pose_array_zero.poses.pop_back();
     }

  //add px,py into vector
  geometry_msgs::Pose temp_pose;
  temp_pose.position.x = projected_0_1.x;
  temp_pose.position.y = projected_0_1.y;
  bbox_pose_array_zero.poses.push_back(temp_pose);
  temp_pose.position.x = projected_0_2.x;
  temp_pose.position.y = projected_0_2.y;
  bbox_pose_array_zero.poses.push_back(temp_pose);

  //empty previous frame data
  while (!bbox_pose_array_one.poses.empty())
     {
     bbox_pose_array_one.poses.pop_back();
     }

  //add px,py into vector
  temp_pose.position.x = projected_1_0.x;
  temp_pose.position.y = projected_1_0.y;
  bbox_pose_array_one.poses.push_back(temp_pose);
  temp_pose.position.x = projected_1_2.x;
  temp_pose.position.y = projected_1_2.y;
  bbox_pose_array_one.poses.push_back(temp_pose);

  //empty previous frame data
  while (!bbox_pose_array_two.poses.empty())
     {
     bbox_pose_array_two.poses.pop_back();
     }

  //add px,py into vector
  temp_pose.position.x = projected_2_0.x;
  temp_pose.position.y = projected_2_0.y;
  bbox_pose_array_two.poses.push_back(temp_pose);
  temp_pose.position.x = projected_2_1.x;
  temp_pose.position.y = projected_2_1.y;
  bbox_pose_array_two.poses.push_back(temp_pose);

  //=====================================

  bbox_pose_array_zero.header.stamp = ros::Time::now();
  bbox_pose_array_one.header.stamp = ros::Time::now();
  bbox_pose_array_two.header.stamp = ros::Time::now();



  //publish 2D track vector
  track_bounding_box_pub_zero.publish(bbox_pose_array_zero);
  track_bounding_box_pub_one.publish(bbox_pose_array_one);
  track_bounding_box_pub_two.publish(bbox_pose_array_two);


}

bool FlightPilot::setUnity(const bool render) {
  unity_render_ = render;
  if (unity_render_ && unity_bridge_ptr_ == nullptr) {
    // create unity bridge
    unity_bridge_ptr_ = UnityBridge::getInstance();

#ifdef use_multi
    unity_bridge_ptr_->addQuadrotor(quad_ptr_1_);
    unity_bridge_ptr_->addQuadrotor(quad_ptr_2_);

#endif

    unity_bridge_ptr_->addQuadrotor(quad_ptr_);

    ROS_WARN("[%s] Unity Bridge is created.", pnh_.getNamespace().c_str());
  }
  return true;
}

bool FlightPilot::connectUnity() {
  if (!unity_render_ || unity_bridge_ptr_ == nullptr) return false;
  unity_ready_ = unity_bridge_ptr_->connectUnity(scene_id_);
  return unity_ready_;
}

bool FlightPilot::loadParams(void) {
  // load parameters
  quadrotor_common::getParam("main_loop_freq", main_loop_freq_, pnh_);
  quadrotor_common::getParam("unity_render", unity_render_, pnh_);

  return true;
}

}  // namespace flightros
