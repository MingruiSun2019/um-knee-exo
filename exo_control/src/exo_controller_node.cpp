// Copyright 2019 Carlos San Vicente
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>
#include <vector>
#include <memory>

#include "rclcpp/strategies/message_pool_memory_strategy.hpp"
#include "rclcpp/strategies/allocator_memory_strategy.hpp"

#include "exo_controller/exo_controller_node.hpp"

namespace exo
{
namespace exo_controller
{

// Delegation Constructor
ExoControllerNode::ExoControllerNode(const rclcpp::NodeOptions & options)
: ExoControllerNode("exo_controller", options)
{}

// Primary Constructor
ExoControllerNode::ExoControllerNode(
  const std::string & node_name,
  const rclcpp::NodeOptions & options)
: LifecycleNode(node_name, options),
  state_topic_name_(declare_parameter<std::string>("state_topic_name", "exo_joint_states")),
  command_topic_name_(declare_parameter<std::string>("command_topic_name", "joint_command")),
  teleop_topic_name_(declare_parameter<std::string>("teleop_topic_name", "teleop")),
  enable_topic_stats_(declare_parameter<bool>("enable_topic_stats", false)),
  topic_stats_topic_name_{declare_parameter<std::string>(
      "topic_stats_topic_name",
      "controller_stats")},
  topic_stats_publish_period_{std::chrono::milliseconds {
        declare_parameter<std::uint16_t>("topic_stats_publish_period_ms", 1000U)}},
  deadline_duration_{std::chrono::milliseconds {
        declare_parameter<std::uint16_t>("deadline_duration_ms", 0U)}},
  controller_(ExoController::Config(
      declare_parameter<std::vector<double>>("controller.feedback_matrix",
      {-10.0000, -51.5393, 356.8637, 154.4146}))),
  num_missed_deadlines_pub_{0U},
  num_missed_deadlines_sub_{0U}
{
  create_teleoperation_subscription();
  create_state_subscription();
  create_command_publisher();
}

void ExoControllerNode::create_teleoperation_subscription()
{
  auto on_exo_teleop = [this](const exo2_msgs::msg::ExoTeleop::SharedPtr msg) {
      controller_.set_teleop(msg->cart_position, msg->cart_velocity);
    };
  teleop_sub_ = this->create_subscription<exo2_msgs::msg::ExoTeleop>(
    teleop_topic_name_, rclcpp::QoS(10), on_exo_teleop);
}

void ExoControllerNode::create_state_subscription()
{
  // Pre-allocates message in a pool
  using rclcpp::strategies::message_pool_memory_strategy::MessagePoolMemoryStrategy;
  using rclcpp::memory_strategies::allocator_memory_strategy::AllocatorMemoryStrategy;
  auto state_msg_strategy =
    std::make_shared<MessagePoolMemoryStrategy<exo2_msgs::msg::JointState, 1>>();

  rclcpp::SubscriptionOptions state_subscription_options;
  state_subscription_options.event_callbacks.deadline_callback =
    [this](rclcpp::QOSDeadlineRequestedInfo &) -> void
    {
      num_missed_deadlines_sub_++;
    };
  if (enable_topic_stats_) {
    state_subscription_options.topic_stats_options.state = rclcpp::TopicStatisticsState::Enable;
    state_subscription_options.topic_stats_options.publish_topic = topic_stats_topic_name_;
    state_subscription_options.topic_stats_options.publish_period = topic_stats_publish_period_;
  }
  auto on_sensor_message = [this](const exo2_msgs::msg::JointState::SharedPtr msg) {
      // update exo state
      controller_.set_state(
        msg->cart_position, msg->cart_velocity,
        msg->pole_angle, msg->pole_velocity);

      // update exo controller output
      controller_.update();

      // publish exo force command
      command_message_.force = controller_.get_force_command();
      command_pub_->publish(command_message_);
    };
  state_sub_ = this->create_subscription<exo2_msgs::msg::JointState>(
    state_topic_name_,
    rclcpp::QoS(10).deadline(deadline_duration_),
    on_sensor_message,
    state_subscription_options,
    state_msg_strategy);
}

void ExoControllerNode::create_command_publisher()
{
  rclcpp::PublisherOptions command_publisher_options;
  command_publisher_options.event_callbacks.deadline_callback =
    [this](rclcpp::QOSDeadlineOfferedInfo &) -> void
    {
      num_missed_deadlines_pub_++;
    };
  command_pub_ = this->create_publisher<exo2_msgs::msg::JointCommand>(
    command_topic_name_,
    rclcpp::QoS(10).deadline(deadline_duration_),
    command_publisher_options);
}

void ExoControllerNode::log_controller_state()
{
  const auto state = controller_.get_state();
  const auto teleoperation_command = controller_.get_teleop();
  const double force_command = controller_.get_force_command();

  RCLCPP_INFO(get_logger(), "Cart position = %lf", state.at(0));
  RCLCPP_INFO(get_logger(), "Cart velocity = %lf", state.at(1));
  RCLCPP_INFO(get_logger(), "Pole angle = %lf", state.at(2));
  RCLCPP_INFO(get_logger(), "Pole angular velocity = %lf", state.at(3));
  RCLCPP_INFO(get_logger(), "Teleoperation cart position = %lf", teleoperation_command.at(0));
  RCLCPP_INFO(get_logger(), "Teleoperation cart velocity = %lf", teleoperation_command.at(1));
  RCLCPP_INFO(get_logger(), "Force command = %lf", force_command);
  RCLCPP_INFO(get_logger(), "Publisher missed deadlines = %u", num_missed_deadlines_pub_);
  RCLCPP_INFO(get_logger(), "Subscription missed deadlines = %u", num_missed_deadlines_sub_);
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
ExoControllerNode::on_configure(const rclcpp_lifecycle::State &)
{
  RCLCPP_INFO(get_logger(), "Configuring");
  // reset internal state of the controller for a clean start
  controller_.reset();
  return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
ExoControllerNode::on_activate(const rclcpp_lifecycle::State &)
{
  RCLCPP_INFO(get_logger(), "Activating");
  command_pub_->on_activate();
  return LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
ExoControllerNode::on_deactivate(const rclcpp_lifecycle::State &)
{
  RCLCPP_INFO(get_logger(), "Deactivating");
  command_pub_->on_deactivate();
  // log the status to introspect the result
  log_controller_state();
  return LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
ExoControllerNode::on_cleanup(const rclcpp_lifecycle::State &)
{
  RCLCPP_INFO(get_logger(), "Cleaning up");
  return LifecycleNodeInterface::CallbackReturn::SUCCESS;
}

rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
ExoControllerNode::on_shutdown(const rclcpp_lifecycle::State &)
{
  RCLCPP_INFO(get_logger(), "Shutting down");
  return LifecycleNodeInterface::CallbackReturn::SUCCESS;
}
}  // namespace exo_controller
}  // namespace exo

#include "rclcpp_components/register_node_macro.hpp"

// Register the component with class_loader.
// This acts as a sort of entry point, allowing the component to be discoverable when its library
// is being loaded into a running process.
RCLCPP_COMPONENTS_REGISTER_NODE(exo::exo_controller::ExoControllerNode)
