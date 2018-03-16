/*
 * This file is part of ElasticFusion.
 *
 * Copyright (C) 2015 Imperial College London
 *
 * The use of the code within this file and all code within files that
 * make up the software that is ElasticFusion is permitted for
 * non-commercial purposes only.  The full terms and conditions that
 * apply to the code within this file are detailed within the LICENSE.txt
 * file and at <http://www.imperial.ac.uk/dyson-robotics-lab/downloads/elastic-fusion/elastic-fusion-license/>
 * unless explicitly stated.  By downloading this file you agree to
 * comply with these terms.
 *
 * If you wish to use any of this code for commercial purposes then
 * please email researchcontracts.engineering@imperial.ac.uk.
 *
 */

#include "SpartanLogReader.h"

bool isSpartanLog(std::string const& value)
{
    std::string ending = ".bag";
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void spartanGetParams(const SpartanLogData & log_data, int& pixels_width, int& pixels_height, double& fx, double& fy, double& cx, double& cy) {
    rosbag::Bag bag;
    
    bag.open(log_data.ros_bag_filename, rosbag::bagmode::Read);
    
    // Pete ToDo: provide CLI for setting topic names (done by manuelli!)
    std::string cam_info_topic = log_data.cam_info_topic;
    
    std::vector<std::string> topics;
    topics.push_back(cam_info_topic);
    rosbag::View view(bag, rosbag::TopicQuery(topics));

    BOOST_FOREACH(rosbag::MessageInstance const m, view)
    { 
        if (m.getTopic() == cam_info_topic || ("/" + m.getTopic() == cam_info_topic)) {
            sensor_msgs::CameraInfo::ConstPtr cam_info = m.instantiate<sensor_msgs::CameraInfo>();
            if (cam_info != NULL) {
                pixels_width = cam_info->width;
                pixels_height = cam_info->height;
                fx = cam_info->K[0];
                fy = cam_info->K[4];
                cx = cam_info->K[2];
                cy = cam_info->K[5];
                bag.close();
                return;
            }
        }
    }
    std::cout << "Did not find camera info!" << std::endl;
    exit(0);
}

void loadBag(const SpartanLogData & log_data, SpartanRgbdData& log_rgbd_data)
{
  rosbag::Bag bag;
  bag.open(log_data.ros_bag_filename, rosbag::bagmode::Read);

  // Pete ToDo: provice CLI for setting topic names
  // std::string image_d_topic = "/camera_carmine_1/depth_registered/sw_registered/image_rect";
  // std::string image_rgb_topic = "/camera_carmine_1/rgb/image_rect_color";
  // std::string cam_info_topic = "/camera_carmine_1/rgb/camera_info";
  
  std::string image_d_topic = log_data.image_depth_topic;
  std::string image_rgb_topic = log_data.image_rgb_topic;
  std::string cam_info_topic = log_data.cam_info_topic;

  std::vector<std::string> topics;
  topics.push_back(image_d_topic);
  topics.push_back(image_rgb_topic);
  topics.push_back(cam_info_topic);
  
  rosbag::View view(bag, rosbag::TopicQuery(topics));
  
  BOOST_FOREACH(rosbag::MessageInstance const m, view)
  {
    if (m.getTopic() == image_d_topic || ("/" + m.getTopic() == image_d_topic))
    {
      sensor_msgs::Image::ConstPtr l_img = m.instantiate<sensor_msgs::Image>();
      if (l_img != NULL) {
        log_rgbd_data.images_d.push_back(l_img);
      }
    }
    
    if (m.getTopic() == image_rgb_topic || ("/" + m.getTopic() == image_rgb_topic))
    {
      sensor_msgs::Image::ConstPtr r_img = m.instantiate<sensor_msgs::Image>();
      if (r_img != NULL) {
        log_rgbd_data.images_rgb.push_back(r_img);
      }
    }
    
    if (m.getTopic() == cam_info_topic || ("/" + m.getTopic() == cam_info_topic))
    {
      sensor_msgs::CameraInfo::ConstPtr r_info = m.instantiate<sensor_msgs::CameraInfo>();
      if (r_info != NULL) {
        log_rgbd_data.cam_info = r_info;
      }
    }
  }
  bag.close();
  std::cout << "rgb data size " << log_rgbd_data.images_rgb.size() << std::endl;
  std::cout << "d data size " << log_rgbd_data.images_d.size() << std::endl;

  unsigned int size_rgb = log_rgbd_data.images_rgb.size();
  unsigned int size_d = log_rgbd_data.images_d.size();
  while(size_rgb != size_d) {
    if(size_rgb > size_d) {
      log_rgbd_data.images_rgb.pop_back();
      size_rgb -= 1;
    }
    else {
      log_rgbd_data.images_d.pop_back();
      size_d -= 1;
    }
  }

  std::cout << "After remove redundant records:" << std::endl;
  std::cout << "rgb data size " << log_rgbd_data.images_rgb.size() << std::endl;
  std::cout << "d data size " << log_rgbd_data.images_d.size() << std::endl;
}


SpartanLogReader::SpartanLogReader(const SpartanLogData & log_data, bool flipColors)
 : LogReader(log_data.ros_bag_filename, flipColors)
{
    assert(pangolin::FileExists(log_data.ros_bag_filename.c_str()));
    
    // Load in all of the ros bag into an ROSRgbdData strcut
    loadBag(log_data, log_rgbd_data);

    // not sure why we need to do this . . . 
    fp = fopen(log_data.ros_bag_filename.c_str(), "rb");

    currentFrame = 0;

    // Pete ToDo: need to implement time sync
    // if (log_rgbd_data.images_rgb.size() != log_rgbd_data.images_d.size()) {
    //   std::cout << "Need to implement time sync!" << std::endl;
    //   exit(0);
    // }

    numFrames = log_rgbd_data.images_rgb.size();

    depthReadBuffer = new unsigned char[numPixels * 2];
    imageReadBuffer = new unsigned char[numPixels * 3];
    decompressionBufferDepth = new Bytef[numPixels * 2];
    decompressionBufferImage = new Bytef[numPixels * 3];
}

SpartanLogReader::~SpartanLogReader()
{
    delete [] depthReadBuffer;
    delete [] imageReadBuffer;
    delete [] decompressionBufferDepth;
    delete [] decompressionBufferImage;

    fclose(fp);
}

void SpartanLogReader::getBack()
{
    currentFrame = numFrames;
    getCore();
}

void SpartanLogReader::getNext()
{
    getCore();
}

std::string ZeroPadNumber(int num)
{
    std::stringstream ss;
    
    // the number is converted to string with the help of stringstream
    ss << num; 
    std::string ret;
    ss >> ret;
    
    // Append zero chars
    int str_length = ret.length();
    for (int i = 0; i < 6 - str_length; i++)
        ret = "0" + ret;
    return ret;
}

void SpartanLogReader::getCore()
{

    std::cout << "In SpartanLogReader getCore" << std::endl;
    std::cout << "current frame " << currentFrame << std::endl;
    std::cout << "padded " << ZeroPadNumber(currentFrame) << std::endl;

    if (currentFrame < 4)
    {
        using namespace png; 

        //Constructor with one string parameter: open a png file
        image<rgb_pixel> img("/home/peteflo/spartan/sandbox/fusion/fusion_1521222309.47/images/"+ZeroPadNumber(currentFrame)+"_rgb.png");

        for(int i=0;i<img.get_width();i++)
        {
            for(int j=0;j<img.get_height();j++)
            {
                rgb_pixel pixel=img.get_pixel(i,j);
                //Replace blue pixels with yellow pixels
                if(pixel.blue>0)
                {
                    rgb_pixel newPixel(pixel.blue,pixel.blue,0);
                    img.set_pixel(i,j,newPixel);
                }
            }
        }

        img.write("/home/peteflo/spartan/sandbox/fusion/fusion_1521222309.47/images/"+ZeroPadNumber(currentFrame)+"_rgbnew.png");
    }  // end namespace png

    timestamp = log_rgbd_data.images_rgb.at(currentFrame)->header.stamp.toNSec();
    depthSize = log_rgbd_data.images_d.at(currentFrame)->step * log_rgbd_data.images_d.at(currentFrame)->height;
    imageSize = log_rgbd_data.images_rgb.at(currentFrame)->step * log_rgbd_data.images_rgb.at(currentFrame)->height;

    // Depth 
    if ((depthSize == numPixels * 4) && (log_rgbd_data.images_d.at(currentFrame)->encoding == "32FC1")) 
    {
        cv::Mat cv_depth;
        cv_depth = cv::imread("/home/peteflo/spartan/sandbox/fusion/fusion_1521222309.47/images/"+ZeroPadNumber(currentFrame)+"_depth.png", CV_16UC1);
        memcpy(&decompressionBufferDepth[0], cv_depth.ptr(), numPixels * 2);
    }
    else
    {
        std::cout << "Am expecting 32FC1 encoded depth image in ROSBagReader.cpp" << std::endl;
        exit(0);
    }


    // RGB
    if (imageSize == numPixels * 3)
    {
        cv::Mat cv_rgb;
        cv_rgb = cv::imread("/home/peteflo/spartan/sandbox/fusion/fusion_1521222309.47/images/"+ZeroPadNumber(currentFrame)+"_rgb.png", cv::IMREAD_COLOR);
        memcpy(&decompressionBufferImage[0], cv_rgb.ptr(), numPixels * 3);
    } else {
        std::cout << "Am not expecting compressed images in ROSBagReader.cpp" << std::endl;
        exit(0);
    }

    rgb = (unsigned char *)&decompressionBufferImage[0];
    depth = (unsigned short *)&decompressionBufferDepth[0];

    if(flipColors)
    {
        for(int i = 0; i < Resolution::getInstance().numPixels() * 3; i += 3)
        {
            std::swap(rgb[i + 0], rgb[i + 2]);
        }
    }

    currentFrame++;
}

void SpartanLogReader::fastForward(int frame)
{
  std::cout << "ROSBagReader::fastForward not implemented" << std::endl;
}

int SpartanLogReader::getNumFrames()
{
    return numFrames;
}

bool SpartanLogReader::hasMore()
{
    return currentFrame + 1 < numFrames;
}


void SpartanLogReader::rewind()
{
    currentFrame = 0;
}

bool SpartanLogReader::rewound()
{
    return (currentFrame == 0);
}

const std::string SpartanLogReader::getFile()
{
    return file;
}

void SpartanLogReader::setAuto(bool value)
{

}
