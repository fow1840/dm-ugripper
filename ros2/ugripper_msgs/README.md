# ugripper_msgs

ROS 2 interface package for UGripper episode MCAP files.

The `Encoder` message matches `/encoder_left` and `/encoder_right` in data
format `3.2_mcap`:

```text
std_msgs/Header header
int32 raw
float32 rad
```

`header.stamp` is identical to the MCAP message `logTime`. `header.frame_id`
is empty because the left/right identity is already carried by the topic name.

Build it in a ROS 2 workspace before playing or decoding an episode:

```bash
mkdir -p ~/ugripper_ros_ws/src
cp -a /opt/ugripper/ros2/ugripper_msgs ~/ugripper_ros_ws/src/
cd ~/ugripper_ros_ws
source /opt/ros/humble/setup.bash
colcon build --packages-select ugripper_msgs
source install/setup.bash
ros2 interface show ugripper_msgs/msg/Encoder
```
