#include "rsdriver.h"

namespace rs_driver
{

rslidarDriver::rslidarDriver(ros::NodeHandle node, ros::NodeHandle private_nh)
    :pointcloud(new pcl::PointCloud<pcl::PointXYZI>)

{
    // use private node handle to get parameters
    private_nh.param("frame_id", config_.frame_id, std::string("rslidar"));

    std::string tf_prefix = tf::getPrefixParam(private_nh);
    ROS_DEBUG_STREAM("tf_prefix: " << tf_prefix);
    config_.frame_id = tf::resolve(tf_prefix, config_.frame_id);

    // get model name, validate string, determine packet rate
    private_nh.param("model", config_.model, std::string("RS16"));
    double packet_rate;                   // packet frequency (Hz)
    std::string model_full_name;


    //不同型号的产品
  if (config_.model == "RS16")
    {
        packet_rate = 834;
        model_full_name = "RS_16";
    }
    else
    {
        ROS_ERROR_STREAM("unknown LIDAR model: " << config_.model);
        packet_rate = 2600.0;
    }
    std::string deviceName(std::string("rslidar ") + model_full_name);

    private_nh.param("rpm", config_.rpm, 600.0);
    //ROS_INFO_STREAM(deviceName << " rotating at " << config_.rpm << " RPM");
    double frequency = (config_.rpm / 60.0);     // expected Hz rate

    // default number of packets for each scan is a single revolution
    // (fractions rounded up)
   /* config_.npackets = (int) ceil(packet_rate / frequency);
    private_nh.getParam("npackets", config_.npackets);
    */
    int npackets = (int) ceil(packet_rate / frequency);
    private_nh.param("npackets", config_.npackets, npackets);
    ROS_INFO_STREAM("publishing " << config_.npackets << " packets per scan");

    std::string dump_file;
    private_nh.param("pcap", dump_file, std::string(""));

    int udp_port;
    private_nh.param("port", udp_port, (int) DATA_PORT_NUMBER);

    // Initialize dynamic reconfigure
    srv_ = boost::make_shared <dynamic_reconfigure::Server<rslidar::
            rslidarNodeConfig> > (private_nh);
    dynamic_reconfigure::Server<rslidar::rslidarNodeConfig>::
            CallbackType f;
    f = boost::bind (&rslidarDriver::callback, this, _1, _2);
    srv_->setCallback (f); // Set callback function und call initially

    // initialize diagnostics
    diagnostics_.setHardwareID(deviceName);
    const double diag_freq = packet_rate/config_.npackets;
    diag_max_freq_ = diag_freq;
    diag_min_freq_ = diag_freq;
    //ROS_INFO("expected frequency: %.3f (Hz)", diag_freq);

    using namespace diagnostic_updater;
    diag_topic_.reset(new TopicDiagnostic("rslidar_packets", diagnostics_,
                                          FrequencyStatusParam(&diag_min_freq_,
                                                               &diag_max_freq_,
                                                               0.1, 10),
                                          TimeStampStatusParam()));

    // open rslidar input device or file
    if (dump_file != "")                  // have PCAP file?
    {
        // read data from packet capture file
        input_.reset(new rs_driver::InputPCAP(private_nh, udp_port,packet_rate, dump_file));
    }
    else
    {
        // read data from live socket
        input_.reset(new rs_driver::InputSocket(private_nh, udp_port));
    }

    // raw packet output topic
    output_ = node.advertise<rslidar::rslidarScan>("rslidar_packets", 10);

    pc_output = node.advertise<sensor_msgs::PointCloud2>("fullscan", 10);
    //pc_output_remove = node.advertise<sensor_msgs::PointCloud2>("remove", 10);

    init_setup();
    loadConfigFile();

}



/** poll the device
 *
 *  @returns true unless end of file reached
 */
bool rslidarDriver::poll(void)
{
    // Allocate a new shared pointer for zero-copy sharing with other nodelets.//一次处理一个360扫描
    rslidar::rslidarScanPtr scan(new rslidar::rslidarScan);
    scan->packets.resize(config_.npackets);
    // Since the rslidar delivers data at a very high rate, keep
    // reading and publishing scans as fast as possible.
    for (int i = 0; i < config_.npackets; ++i)
    {
        while (true)
        {
            // keep reading until full packet received
            int rc = input_->getPacket(&scan->packets[i], config_.time_offset);
            if (rc == 0) break;       // got a full packet?
            if (rc < 0) return false; // end of file reached?
        }
    }
   // ROS_INFO("poll2");
    // publish message using time of last packet read
    ROS_DEBUG("Publishing a full rslidar scan.");
    scan->header.stamp = scan->packets[config_.npackets - 1].stamp;
    scan->header.frame_id = config_.frame_id;
   // output_.publish(scan);
   ;
    for(size_t i=0; i<scan->packets.size(); ++i)
    {
        unpack(scan->packets[i],pointcloud);

    }
    sensor_msgs::PointCloud2 out;
    pcl::toROSMsg(*pointcloud, out);
    pc_output.publish(out);

    //sensor_msgs::PointCloud2 out2;
    //std::cout << "PCloudRemove.size " << PCloudRemove->size() <<std::endl;
    //pcl::toROSMsg(*PCloudRemove, out2);
    //pc_output_remove.publish(out2);

    // notify diagnostics that a message has been published, updating its status
    diag_topic_->tick(scan->header.stamp);
    diagnostics_.update();

    return true;
}

void rslidarDriver::callback(rslidar::rslidarNodeConfig &config,
                              uint32_t level)
{
    ROS_INFO("Reconfigure Request");
    config_.time_offset = config.time_offset;
}

}