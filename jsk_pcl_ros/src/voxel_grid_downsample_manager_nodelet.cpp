/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013, Ryohei Ueda and JSK Lab
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/o2r other materials provided
 *     with the distribution.
 *   * Neither the name of the Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

#include "jsk_pcl_ros/voxel_grid_downsample_manager.h"
#include <pluginlib/class_list_macros.h>
#include <pcl/point_types.h>
#include <pcl/filters/passthrough.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/io/io.h>
#include <pcl_ros/transforms.h>
#include <boost/format.hpp>

namespace jsk_pcl_ros
{

  void VoxelGridDownsampleManager::clearAll()
  {
    grid_.clear();
  }
  
  
  void VoxelGridDownsampleManager::pointCB(const sensor_msgs::PointCloud2ConstPtr &input)
  {
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr output_cloud (new pcl::PointCloud<pcl::PointXYZ>);
    fromROSMsg(*input, *cloud);
    for (size_t i = 0; i < grid_.size(); i++)
    {
      visualization_msgs::Marker::ConstPtr target_grid = grid_[i];
      // not yet tf is supported
      int id = target_grid->id;
      // solve tf with ros::Time 0.0

      // transform pointcloud to the frame_id of target_grid
      pcl::PointCloud<pcl::PointXYZ>::Ptr transformed_cloud (new pcl::PointCloud<pcl::PointXYZ>);
      pcl_ros::transformPointCloud(target_grid->header.frame_id,
                                   *cloud,
                                   *transformed_cloud,
                                   tf_listener);
      double center_x = target_grid->pose.position.x;
      double center_y = target_grid->pose.position.y;
      double center_z = target_grid->pose.position.z;
      double range_x = target_grid->scale.x * 1.0; // for debug
      double range_y = target_grid->scale.y * 1.0;
      double range_z = target_grid->scale.z * 1.0;
      double min_x = center_x - range_x / 2.0;
      double max_x = center_x + range_x / 2.0;
      double min_y = center_y - range_y / 2.0;
      double max_y = center_y + range_y / 2.0;
      double min_z = center_z - range_z / 2.0;
      double max_z = center_z + range_z / 2.0;
      double resolution = target_grid->color.r;
      // filter order: x -> y -> z -> downsample
      pcl::PassThrough<pcl::PointXYZ> pass_x;
      pass_x.setFilterFieldName("x");
      pass_x.setFilterLimits(min_x, max_x);
      
      pcl::PassThrough<pcl::PointXYZ> pass_y;
      pass_y.setFilterFieldName("y");
      pass_y.setFilterLimits(min_y, max_y);
      pcl::PassThrough<pcl::PointXYZ> pass_z;
      pass_z.setFilterFieldName("z");
      pass_z.setFilterLimits(min_z, max_z);

      ROS_INFO_STREAM(id << " filter x: " << min_x << " - " << max_x);
      ROS_INFO_STREAM(id << " filter y: " << min_y << " - " << max_y);
      ROS_INFO_STREAM(id << " filter z: " << min_z << " - " << max_z);
      
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_after_x (new pcl::PointCloud<pcl::PointXYZ>);
      pass_x.setInputCloud (transformed_cloud);
      pass_x.filter(*cloud_after_x);

      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_after_y (new pcl::PointCloud<pcl::PointXYZ>);
      pass_y.setInputCloud (cloud_after_x);
      pass_y.filter(*cloud_after_y);

      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_after_z (new pcl::PointCloud<pcl::PointXYZ>);
      pass_z.setInputCloud (cloud_after_y);
      pass_z.filter(*cloud_after_z);

      // downsample
      pcl::VoxelGrid<pcl::PointXYZ> sor;
      sor.setInputCloud (cloud_after_z);
      sor.setLeafSize (resolution, resolution, resolution);
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered (new pcl::PointCloud<pcl::PointXYZ>);
      sor.filter (*cloud_filtered);

      // reverse transform
      pcl::PointCloud<pcl::PointXYZ>::Ptr reverse_transformed_cloud (new pcl::PointCloud<pcl::PointXYZ>);
      pcl_ros::transformPointCloud(input->header.frame_id,
                                   *cloud_filtered,
                                   *reverse_transformed_cloud,
                                   tf_listener);
      
      // adding the output into *output_cloud
      // tmp <- cloud_filtered + output_cloud
      // output_cloud <- tmp
      //pcl::PointCloud<pcl::PointXYZ>::Ptr tmp (new pcl::PointCloud<pcl::PointXYZ>);
      //pcl::concatenatePointCloud (*cloud_filtered, *output_cloud, tmp);
      //output_cloud = tmp;
      ROS_INFO_STREAM(id << " includes " << reverse_transformed_cloud->points.size() << " points");
      for (size_t i = 0; i < reverse_transformed_cloud->points.size(); i++) {
        output_cloud->points.push_back(reverse_transformed_cloud->points[i]);
      }
    }
    sensor_msgs::PointCloud2 out;
    toROSMsg(*output_cloud, out);
    out.header = input->header;
    pub_.publish(out);          // for debugging

    // for concatenater
    size_t cluster_num = output_cloud->points.size() / max_points_ + 1;
    ROS_INFO_STREAM("encoding into " << cluster_num << " clusters");
    for (size_t i = 0; i < cluster_num; i++) {
      size_t start_index = max_points_ * i;
      size_t end_index = max_points_ * (i + 1) > output_cloud->points.size() ?
        output_cloud->points.size(): max_points_ * (i + 1);
      sensor_msgs::PointCloud2 cluster_out_ros;
      pcl::PointCloud<pcl::PointXYZ>::Ptr
        cluster_out_pcl(new pcl::PointCloud<pcl::PointXYZ>);
      cluster_out_pcl->points.resize(end_index - start_index);
      // build cluster_out_pcl
      ROS_INFO_STREAM("make cluster from " << start_index << " to " << end_index);
      for (size_t j = start_index; j < end_index; j++) {
        cluster_out_pcl->points[j - start_index] = output_cloud->points[j];
      }
      // conevrt cluster_out_pcl into ros msg
      toROSMsg(*cluster_out_pcl, cluster_out_ros);
      cluster_out_ros.header = input->header;
      cluster_out_ros.header.frame_id = (boost::format("%s %d") % (cluster_out_ros.header.frame_id) % (i)).str();
      pub_encoded_.publish(cluster_out_ros);
      ros::Duration(1.0 / rate_).sleep();
    }
  }

  void VoxelGridDownsampleManager::addGrid(const visualization_msgs::Marker::ConstPtr &new_box)
  {
    // check we have new_box->id in our bounding_boxes_
    if (new_box->id == -1) {
      // cancel all
      ROS_INFO("clear all pointclouds");
      clearAll();
    }
    else {
      for (size_t i = 0; i < grid_.size(); i++) {
        if (grid_[i]->id == new_box->id) {
          ROS_INFO_STREAM("updating " << new_box->id << " grid");
          grid_[i] = new_box;
        }
      }
      ROS_INFO_STREAM("adding new grid: " << new_box->id);
      grid_.push_back(new_box);
    }
  }
  
  void VoxelGridDownsampleManager::onInit(void)
  {
    PCLNodelet::onInit();

    sub_ = pnh_->subscribe("input", 1, &VoxelGridDownsampleManager::pointCB,
                           this);
    bounding_box_sub_ = pnh_->subscribe("add_grid", 1, &VoxelGridDownsampleManager::addGrid,
                                        this);
    pub_ = pnh_->advertise<sensor_msgs::PointCloud2>("output", 1);
    pub_encoded_ = pnh_->advertise<sensor_msgs::PointCloud2>("output_encoded", 1);
    max_points_ = 300;
    rate_ = 1.0;                // 1Hz
  }
}

typedef jsk_pcl_ros::VoxelGridDownsampleManager VoxelGridDownsampleManager;
PLUGINLIB_DECLARE_CLASS (jsk_pcl, VoxelGridDownsampleManager, VoxelGridDownsampleManager, nodelet::Nodelet);