#include <rosbag/bag.h>
#include <rosbag/view.h>
#include "convert.h"

/** Main node entry point. */
int main(int argc, char** argv)
{
  ros::init(argc, argv, "cloud_bag");
  ros::NodeHandle node;
  ros::NodeHandle priv_nh("~");

  rslidar_pointcloud::BagConvert conv(node, priv_nh);

  rosbag::Bag in_bag;
  in_bag.open(conv.bag_path_);

  // TODO: out name fixed
  rosbag::Bag out_bag(conv.bag_path_.substr(0, conv.bag_path_.size() - 4) + "_converted.bag", rosbag::bagmode::Write);

  for(rosbag::MessageInstance const m: rosbag::View(in_bag)) {
    auto i = m.instantiate<rslidar_msgs::rslidarScan>();
    if (i != NULL) {
      sensor_msgs::PointCloud2::Ptr cloud_msg = conv.processScan(i);

      // TODO: hard coded
      out_bag.write("/rslidar_points", cloud_msg->header.stamp, cloud_msg);
    } else {
      out_bag.write(m.getTopic(), m.getTime(), m, m.getConnectionHeader());
    }
  }

  in_bag.close();
  out_bag.close();

  return 0;
}
