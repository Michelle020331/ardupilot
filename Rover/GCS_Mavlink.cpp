#include "Rover.h"

#include "GCS_Mavlink.h"

#include <AP_RangeFinder/AP_RangeFinder_Backend.h>

MAV_TYPE GCS_Rover::frame_type() const
{
    if (rover.is_boat()) {
        return MAV_TYPE_SURFACE_BOAT;
    }
    return MAV_TYPE_GROUND_ROVER;
}

MAV_MODE GCS_MAVLINK_Rover::base_mode() const
{
    uint8_t _base_mode = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;

    // work out the base_mode. This value is not very useful
    // for APM, but we calculate it as best we can so a generic
    // MAVLink enabled ground station can work out something about
    // what the MAV is up to. The actual bit values are highly
    // ambiguous for most of the APM flight modes. In practice, you
    // only get useful information from the custom_mode, which maps to
    // the APM flight mode and has a well defined meaning in the
    // ArduPlane documentation
    if (rover.control_mode->has_manual_input()) {
        _base_mode |= MAV_MODE_FLAG_MANUAL_INPUT_ENABLED;
    }

    if (rover.control_mode->is_autopilot_mode()) {
        _base_mode |= MAV_MODE_FLAG_GUIDED_ENABLED;
    }

    if (rover.g2.stick_mixing > 0 && rover.control_mode != &rover.mode_initializing) {
        // all modes except INITIALISING have some form of manual
        // override if stick mixing is enabled
        _base_mode |= MAV_MODE_FLAG_MANUAL_INPUT_ENABLED;
    }

    // we are armed if we are not initialising
    if (rover.control_mode != &rover.mode_initializing && rover.arming.is_armed()) {
        _base_mode |= MAV_MODE_FLAG_SAFETY_ARMED;
    }

    // indicate we have set a custom mode
    _base_mode |= MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;

    return (MAV_MODE)_base_mode;
}

uint32_t GCS_Rover::custom_mode() const
{
    return rover.control_mode->mode_number();
}

MAV_STATE GCS_MAVLINK_Rover::vehicle_system_status() const
{
    if ((rover.failsafe.triggered != 0) || rover.failsafe.ekf) {
        return MAV_STATE_CRITICAL;
    }
    if (rover.control_mode == &rover.mode_initializing) {
        return MAV_STATE_CALIBRATING;
    }
    if (rover.control_mode == &rover.mode_hold) {
        return MAV_STATE_STANDBY;
    }

    return MAV_STATE_ACTIVE;
}

void GCS_MAVLINK_Rover::send_position_target_global_int()
{
    Location target;
    if (!rover.control_mode->get_desired_location(target)) {
        return;
    }
    static constexpr uint16_t POSITION_TARGET_TYPEMASK_LAST_BYTE = 0xF000;
    static constexpr uint16_t TYPE_MASK = POSITION_TARGET_TYPEMASK_VX_IGNORE | POSITION_TARGET_TYPEMASK_VY_IGNORE | POSITION_TARGET_TYPEMASK_VZ_IGNORE |
                                          POSITION_TARGET_TYPEMASK_AX_IGNORE | POSITION_TARGET_TYPEMASK_AY_IGNORE | POSITION_TARGET_TYPEMASK_AZ_IGNORE |
                                          POSITION_TARGET_TYPEMASK_YAW_IGNORE | POSITION_TARGET_TYPEMASK_YAW_RATE_IGNORE | POSITION_TARGET_TYPEMASK_LAST_BYTE;
    mavlink_msg_position_target_global_int_send(
        chan,
        AP_HAL::millis(), // time_boot_ms
        MAV_FRAME_GLOBAL, // targets are always global altitude
        TYPE_MASK, // ignore everything except the x/y/z components
        target.lat, // latitude as 1e7
        target.lng, // longitude as 1e7
        target.alt * 0.01f, // altitude is sent as a float
        0.0f, // vx
        0.0f, // vy
        0.0f, // vz
        0.0f, // afx
        0.0f, // afy
        0.0f, // afz
        0.0f, // yaw
        0.0f); // yaw_rate
}

void GCS_MAVLINK_Rover::send_nav_controller_output() const
{
    if (!rover.control_mode->is_autopilot_mode()) {
        return;
    }

    const Mode *control_mode = rover.control_mode;

    mavlink_msg_nav_controller_output_send(
        chan,
        0,  // roll
        degrees(rover.g2.attitude_control.get_desired_pitch()),
        control_mode->nav_bearing(),
        control_mode->wp_bearing(),
        MIN(control_mode->get_distance_to_destination(), UINT16_MAX),
        0,
        control_mode->speed_error(),
        control_mode->crosstrack_error());
}

void GCS_MAVLINK_Rover::send_servo_out()
{
    float motor1, motor3;
    if (rover.g2.motors.have_skid_steering()) {
        motor1 = 10000 * (SRV_Channels::get_output_scaled(SRV_Channel::k_throttleLeft) / 1000.0f);
        motor3 = 10000 * (SRV_Channels::get_output_scaled(SRV_Channel::k_throttleRight) / 1000.0f);
    } else {
        motor1 = 10000 * (SRV_Channels::get_output_scaled(SRV_Channel::k_steering) / 4500.0f);
        motor3 = 10000 * (SRV_Channels::get_output_scaled(SRV_Channel::k_throttle) / 100.0f);
    }
    mavlink_msg_rc_channels_scaled_send(
        chan,
        millis(),
        0,  // port 0
        motor1,
        0,
        motor3,
        0,
        0,
        0,
        0,
        0,
        receiver_rssi());
}

int16_t GCS_MAVLINK_Rover::vfr_hud_throttle() const
{
    return rover.g2.motors.get_throttle();
}

void GCS_MAVLINK_Rover::send_rangefinder() const
{
    float distance_cm;
    float voltage;
    bool got_one = false;

    // report smaller distance of all rangefinders
    for (uint8_t i=0; i<rover.rangefinder.num_sensors(); i++) {
        AP_RangeFinder_Backend *s = rover.rangefinder.get_backend(i);
        if (s == nullptr) {
            continue;
        }
        if (!got_one ||
            s->distance_cm() < distance_cm) {
            distance_cm = s->distance_cm();
            voltage = s->voltage_mv();
            got_one = true;
        }
    }
    if (!got_one) {
        // no relevant data found
        return;
    }

    mavlink_msg_rangefinder_send(
        chan,
        distance_cm * 0.01f,
        voltage);
}

/*
  send PID tuning message
 */
void GCS_MAVLINK_Rover::send_pid_tuning()
{
    Parameters &g = rover.g;
    ParametersG2 &g2 = rover.g2;

    const AP_Logger::PID_Info *pid_info;

    // steering PID
    if (g.gcs_pid_mask & 1) {
        pid_info = &g2.attitude_control.get_steering_rate_pid().get_pid_info();
        mavlink_msg_pid_tuning_send(chan, PID_TUNING_STEER,
                                    degrees(pid_info->target),
                                    degrees(pid_info->actual),
                                    pid_info->FF,
                                    pid_info->P,
                                    pid_info->I,
                                    pid_info->D,
                                    pid_info->slew_rate,
                                    pid_info->Dmod);
        if (!HAVE_PAYLOAD_SPACE(chan, PID_TUNING)) {
            return;
        }
    }

    // speed to throttle PID
    if (g.gcs_pid_mask & 2) {
        pid_info = &g2.attitude_control.get_throttle_speed_pid().get_pid_info();
        mavlink_msg_pid_tuning_send(chan, PID_TUNING_ACCZ,
                                    pid_info->target,
                                    pid_info->actual,
                                    pid_info->FF,
                                    pid_info->P,
                                    pid_info->I,
                                    pid_info->D,
                                    pid_info->slew_rate,
                                    pid_info->Dmod);
        if (!HAVE_PAYLOAD_SPACE(chan, PID_TUNING)) {
            return;
        }
    }

    // pitch to throttle pid
    if (g.gcs_pid_mask & 4) {
        pid_info = &g2.attitude_control.get_pitch_to_throttle_pid().get_pid_info();
        mavlink_msg_pid_tuning_send(chan, PID_TUNING_PITCH,
                                    degrees(pid_info->target),
                                    degrees(pid_info->actual),
                                    pid_info->FF,
                                    pid_info->P,
                                    pid_info->I,
                                    pid_info->D,
                                    pid_info->slew_rate,
                                    pid_info->Dmod);
        if (!HAVE_PAYLOAD_SPACE(chan, PID_TUNING)) {
            return;
        }
    }

    // left wheel rate control pid
    if (g.gcs_pid_mask & 8) {
        pid_info = &g2.wheel_rate_control.get_pid(0).get_pid_info();
        mavlink_msg_pid_tuning_send(chan, 7,
                                    pid_info->target,
                                    pid_info->actual,
                                    pid_info->FF,
                                    pid_info->P,
                                    pid_info->I,
                                    pid_info->D,
                                    pid_info->slew_rate,
                                    pid_info->Dmod);
        if (!HAVE_PAYLOAD_SPACE(chan, PID_TUNING)) {
            return;
        }
    }

    // right wheel rate control pid
    if (g.gcs_pid_mask & 16) {
        pid_info = &g2.wheel_rate_control.get_pid(1).get_pid_info();
        mavlink_msg_pid_tuning_send(chan, 8,
                                    pid_info->target,
                                    pid_info->actual,
                                    pid_info->FF,
                                    pid_info->P,
                                    pid_info->I,
                                    pid_info->D,
                                    pid_info->slew_rate,
                                    pid_info->Dmod);
        if (!HAVE_PAYLOAD_SPACE(chan, PID_TUNING)) {
            return;
        }
    }

    // sailboat heel to mainsail pid
    if (g.gcs_pid_mask & 32) {
        pid_info = &g2.attitude_control.get_sailboat_heel_pid().get_pid_info();
        mavlink_msg_pid_tuning_send(chan, 9,
                                    pid_info->target,
                                    pid_info->actual,
                                    pid_info->FF,
                                    pid_info->P,
                                    pid_info->I,
                                    pid_info->D,
                                    pid_info->slew_rate,
                                    pid_info->Dmod);
        if (!HAVE_PAYLOAD_SPACE(chan, PID_TUNING)) {
            return;
        }
    }
}

void Rover::send_wheel_encoder_distance(const mavlink_channel_t chan)
{
    // send wheel encoder data using wheel_distance message
    if (g2.wheel_encoder.num_sensors() > 0) {
        double distances[MAVLINK_MSG_WHEEL_DISTANCE_FIELD_DISTANCE_LEN] {};
        for (uint8_t i = 0; i < g2.wheel_encoder.num_sensors(); i++) {
            distances[i] = wheel_encoder_last_distance_m[i];
        }
        mavlink_msg_wheel_distance_send(chan, 1000UL * AP_HAL::millis(), g2.wheel_encoder.num_sensors(), distances);
    }
}

uint8_t GCS_MAVLINK_Rover::sysid_my_gcs() const
{
    return rover.g.sysid_my_gcs;
}

bool GCS_MAVLINK_Rover::sysid_enforce() const
{
    return rover.g2.sysid_enforce;
}

uint32_t GCS_MAVLINK_Rover::telem_delay() const
{
    return static_cast<uint32_t>(rover.g.telem_delay);
}

bool GCS_Rover::vehicle_initialised() const
{
    return rover.control_mode != &rover.mode_initializing;
}

// try to send a message, return false if it won't fit in the serial tx buffer
bool GCS_MAVLINK_Rover::try_send_message(enum ap_message id)
{
    switch (id) {

    case MSG_SERVO_OUT:
        CHECK_PAYLOAD_SIZE(RC_CHANNELS_SCALED);
        send_servo_out();
        break;

    case MSG_WHEEL_DISTANCE:
        CHECK_PAYLOAD_SIZE(WHEEL_DISTANCE);
        rover.send_wheel_encoder_distance(chan);
        break;

    case MSG_WIND:
        CHECK_PAYLOAD_SIZE(WIND);
        rover.g2.windvane.send_wind(chan);
        break;

    case MSG_ADSB_VEHICLE: {
        AP_OADatabase *oadb = AP::oadatabase();
        if (oadb != nullptr) {
            CHECK_PAYLOAD_SIZE(ADSB_VEHICLE);
            uint16_t interval_ms = 0;
            if (get_ap_message_interval(id, interval_ms)) {
                oadb->send_adsb_vehicle(chan, interval_ms);
            }
        }
        break;
    }

    case MSG_AIS_VESSEL: {
#if HAL_AIS_ENABLED
        rover.g2.ais.send(chan);
#endif
        break;
    }

    default:
        return GCS_MAVLINK::try_send_message(id);
    }
    return true;
}

void GCS_MAVLINK_Rover::packetReceived(const mavlink_status_t &status, const mavlink_message_t &msg)
{
    // pass message to follow library
    rover.g2.follow.handle_msg(msg);
    GCS_MAVLINK::packetReceived(status, msg);
}

/*
  default stream rates to 1Hz
 */
const AP_Param::GroupInfo GCS_MAVLINK_Parameters::var_info[] = {
    // @Param: RAW_SENS
    // @DisplayName: Raw sensor stream rate
    // @Description: Raw sensor stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("RAW_SENS", 0, GCS_MAVLINK_Parameters, streamRates[0],  1),

    // @Param: EXT_STAT
    // @DisplayName: Extended status stream rate to ground station
    // @Description: Extended status stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("EXT_STAT", 1, GCS_MAVLINK_Parameters, streamRates[1],  1),

    // @Param: RC_CHAN
    // @DisplayName: RC Channel stream rate to ground station
    // @Description: RC Channel stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("RC_CHAN",  2, GCS_MAVLINK_Parameters, streamRates[2],  1),

    // @Param: RAW_CTRL
    // @DisplayName: Raw Control stream rate to ground station
    // @Description: Raw Control stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("RAW_CTRL", 3, GCS_MAVLINK_Parameters, streamRates[3],  1),

    // @Param: POSITION
    // @DisplayName: Position stream rate to ground station
    // @Description: Position stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("POSITION", 4, GCS_MAVLINK_Parameters, streamRates[4],  1),

    // @Param: EXTRA1
    // @DisplayName: Extra data type 1 stream rate to ground station
    // @Description: Extra data type 1 stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("EXTRA1",   5, GCS_MAVLINK_Parameters, streamRates[5],  1),

    // @Param: EXTRA2
    // @DisplayName: Extra data type 2 stream rate to ground station
    // @Description: Extra data type 2 stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("EXTRA2",   6, GCS_MAVLINK_Parameters, streamRates[6],  1),

    // @Param: EXTRA3
    // @DisplayName: Extra data type 3 stream rate to ground station
    // @Description: Extra data type 3 stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("EXTRA3",   7, GCS_MAVLINK_Parameters, streamRates[7],  1),

    // @Param: PARAMS
    // @DisplayName: Parameter stream rate to ground station
    // @Description: Parameter stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("PARAMS",   8, GCS_MAVLINK_Parameters, streamRates[8],  10),

    // @Param: ADSB
    // @DisplayName: ADSB stream rate to ground station
    // @Description: ADSB stream rate to ground station
    // @Units: Hz
    // @Range: 0 50
    // @Increment: 1
    // @User: Advanced
    AP_GROUPINFO("ADSB",   9, GCS_MAVLINK_Parameters, streamRates[9],  0),

    AP_GROUPEND
};

static const ap_message STREAM_RAW_SENSORS_msgs[] = {
    MSG_RAW_IMU,
    MSG_SCALED_IMU2,
    MSG_SCALED_IMU3,
    MSG_SCALED_PRESSURE,
    MSG_SCALED_PRESSURE2,
    MSG_SCALED_PRESSURE3,
};
static const ap_message STREAM_EXTENDED_STATUS_msgs[] = {
    MSG_SYS_STATUS,
    MSG_POWER_STATUS,
    MSG_MCU_STATUS,
    MSG_MEMINFO,
    MSG_CURRENT_WAYPOINT,
    MSG_GPS_RAW,
    MSG_GPS_RTK,
    MSG_GPS2_RAW,
    MSG_GPS2_RTK,
    MSG_NAV_CONTROLLER_OUTPUT,
    MSG_FENCE_STATUS,
    MSG_POSITION_TARGET_GLOBAL_INT,
};
static const ap_message STREAM_POSITION_msgs[] = {
    MSG_LOCATION,
    MSG_LOCAL_POSITION
};
static const ap_message STREAM_RAW_CONTROLLER_msgs[] = {
    MSG_SERVO_OUT,
};
static const ap_message STREAM_RC_CHANNELS_msgs[] = {
    MSG_SERVO_OUTPUT_RAW,
    MSG_RC_CHANNELS,
    MSG_RC_CHANNELS_RAW, // only sent on a mavlink1 connection
};
static const ap_message STREAM_EXTRA1_msgs[] = {
    MSG_ATTITUDE,
    MSG_SIMSTATE,
    MSG_AHRS2,
    MSG_PID_TUNING,
};
static const ap_message STREAM_EXTRA2_msgs[] = {
    MSG_VFR_HUD
};
static const ap_message STREAM_EXTRA3_msgs[] = {
    MSG_AHRS,
    MSG_HWSTATUS,
    MSG_WIND,
    MSG_RANGEFINDER,
    MSG_DISTANCE_SENSOR,
    MSG_SYSTEM_TIME,
    MSG_BATTERY2,
    MSG_BATTERY_STATUS,
    MSG_MOUNT_STATUS,
    MSG_MAG_CAL_REPORT,
    MSG_MAG_CAL_PROGRESS,
    MSG_EKF_STATUS_REPORT,
    MSG_VIBRATION,
    MSG_RPM,
    MSG_WHEEL_DISTANCE,
    MSG_ESC_TELEMETRY,
};
static const ap_message STREAM_PARAMS_msgs[] = {
    MSG_NEXT_PARAM
};
static const ap_message STREAM_ADSB_msgs[] = {
    MSG_ADSB_VEHICLE,
    MSG_AIS_VESSEL,
};

const struct GCS_MAVLINK::stream_entries GCS_MAVLINK::all_stream_entries[] = {
    MAV_STREAM_ENTRY(STREAM_RAW_SENSORS),
    MAV_STREAM_ENTRY(STREAM_EXTENDED_STATUS),
    MAV_STREAM_ENTRY(STREAM_POSITION),
    MAV_STREAM_ENTRY(STREAM_RAW_CONTROLLER),
    MAV_STREAM_ENTRY(STREAM_RC_CHANNELS),
    MAV_STREAM_ENTRY(STREAM_EXTRA1),
    MAV_STREAM_ENTRY(STREAM_EXTRA2),
    MAV_STREAM_ENTRY(STREAM_EXTRA3),
    MAV_STREAM_ENTRY(STREAM_ADSB),
    MAV_STREAM_ENTRY(STREAM_PARAMS),
    MAV_STREAM_TERMINATOR // must have this at end of stream_entries
};

bool GCS_MAVLINK_Rover::handle_guided_request(AP_Mission::Mission_Command &cmd)
{
    if (!rover.control_mode->in_guided_mode()) {
        // only accept position updates when in GUIDED mode
        return false;
    }

    // make any new wp uploaded instant (in case we are already in Guided mode)
    return rover.mode_guided.set_desired_location(cmd.content.location);
}

void GCS_MAVLINK_Rover::handle_change_alt_request(AP_Mission::Mission_Command &cmd)
{
    // nothing to do
}

MAV_RESULT GCS_MAVLINK_Rover::_handle_command_preflight_calibration(const mavlink_command_long_t &packet)
{
    if (is_equal(packet.param6, 1.0f)) {
        if (rover.g2.windvane.start_direction_calibration()) {
            return MAV_RESULT_ACCEPTED;
        } else {
            return MAV_RESULT_FAILED;
        }
    } else if (is_equal(packet.param6, 2.0f)) {
        if (rover.g2.windvane.start_speed_calibration()) {
            return MAV_RESULT_ACCEPTED;
        } else {
            return MAV_RESULT_FAILED;
        }
    }

    return GCS_MAVLINK::_handle_command_preflight_calibration(packet);
}

bool GCS_MAVLINK_Rover::set_home_to_current_location(bool _lock) {
    return rover.set_home_to_current_location(_lock);
}

bool GCS_MAVLINK_Rover::set_home(const Location& loc, bool _lock) {
    return rover.set_home(loc, _lock);
}

MAV_RESULT GCS_MAVLINK_Rover::handle_command_int_packet(const mavlink_command_int_t &packet)
{
    switch (packet.command) {

    case MAV_CMD_DO_CHANGE_SPEED:
        // param1 : unused
        // param2 : new speed in m/s
        if (!rover.control_mode->set_desired_speed(packet.param2)) {
            return MAV_RESULT_FAILED;
        }
        return MAV_RESULT_ACCEPTED;

    case MAV_CMD_DO_REPOSITION:
        return handle_command_int_do_reposition(packet);

    case MAV_CMD_DO_SET_REVERSE:
        // param1 : Direction (0=Forward, 1=Reverse)
        rover.control_mode->set_reversed(is_equal(packet.param1,1.0f));
        return MAV_RESULT_ACCEPTED;

    // pause or resume an auto mission
    case MAV_CMD_DO_PAUSE_CONTINUE:
        return handle_command_pause_continue(packet);

    default:
        return GCS_MAVLINK::handle_command_int_packet(packet);
    }
}

MAV_RESULT GCS_MAVLINK_Rover::handle_command_long_packet(const mavlink_command_long_t &packet)
{
    switch (packet.command) {

    case MAV_CMD_NAV_RETURN_TO_LAUNCH:
        if (rover.set_mode(rover.mode_rtl, ModeReason::GCS_COMMAND)) {
            return MAV_RESULT_ACCEPTED;
        }
        return MAV_RESULT_FAILED;

    case MAV_CMD_MISSION_START:
        if (rover.set_mode(rover.mode_auto, ModeReason::GCS_COMMAND)) {
            return MAV_RESULT_ACCEPTED;
        }
        return MAV_RESULT_FAILED;

    case MAV_CMD_DO_CHANGE_SPEED:
        // param1 : unused
        // param2 : new speed in m/s
        if (!rover.control_mode->set_desired_speed(packet.param2)) {
            return MAV_RESULT_FAILED;
        }
        return MAV_RESULT_ACCEPTED;

    case MAV_CMD_DO_SET_REVERSE:
        // param1 : Direction (0=Forward, 1=Reverse)
        rover.control_mode->set_reversed(is_equal(packet.param1,1.0f));
        return MAV_RESULT_ACCEPTED;

    case MAV_CMD_NAV_SET_YAW_SPEED:
    {
        // param1 : yaw angle to adjust direction by in centidegress
        // param2 : Speed - normalized to 0 .. 1
        // param3 : 0 = absolute, 1 = relative

        // exit if vehicle is not in Guided mode
        if (!rover.control_mode->in_guided_mode()) {
            return MAV_RESULT_FAILED;
        }

        // get final angle, 1 = Relative, 0 = Absolute
        if (packet.param3 > 0) {
            // relative angle
            rover.mode_guided.set_desired_heading_delta_and_speed(packet.param1 * 100.0f, packet.param2);
        } else {
            // absolute angle
            rover.mode_guided.set_desired_heading_and_speed(packet.param1 * 100.0f, packet.param2);
        }
        return MAV_RESULT_ACCEPTED;
    }

    case MAV_CMD_DO_MOTOR_TEST:
        // param1 : motor sequence number (a number from 1 to max number of motors on the vehicle)
        // param2 : throttle type (0=throttle percentage, 1=PWM, 2=pilot throttle channel pass-through. See MOTOR_TEST_THROTTLE_TYPE enum)
        // param3 : throttle (range depends upon param2)
        // param4 : timeout (in seconds)
        return rover.mavlink_motor_test_start(*this,
                                              (AP_MotorsUGV::motor_test_order)packet.param1,
                                              static_cast<uint8_t>(packet.param2),
                                              static_cast<int16_t>(packet.param3),
                                              packet.param4);

    // pause or resume an auto mission
    case MAV_CMD_DO_PAUSE_CONTINUE: {
        mavlink_command_int_t packet_int;
        GCS_MAVLINK_Rover::convert_COMMAND_LONG_to_COMMAND_INT(packet, packet_int);
        return handle_command_pause_continue(packet_int);
    }

    default:
        return GCS_MAVLINK::handle_command_long_packet(packet);
    }
}

MAV_RESULT GCS_MAVLINK_Rover::handle_command_pause_continue(const mavlink_command_int_t &packet)
{
    // requested pause
    if ((uint8_t) packet.param1 == 0) {
        if (rover.control_mode->pause()) {
            send_text(MAV_SEVERITY_INFO, "Paused");
            return MAV_RESULT_ACCEPTED;
        }
        send_text(MAV_SEVERITY_INFO, "Failed to pause");
        return MAV_RESULT_FAILED;
    }

    // requested resume
    if ((uint8_t) packet.param1 == 1) {
        if (rover.control_mode->resume()) {
            send_text(MAV_SEVERITY_INFO, "Resumed");
            return MAV_RESULT_ACCEPTED;
        }
        send_text(MAV_SEVERITY_INFO, "Failed to resume");
        return MAV_RESULT_FAILED;
    }

    // fail pause or continue
    return MAV_RESULT_DENIED;
}

MAV_RESULT GCS_MAVLINK_Rover::handle_command_int_do_reposition(const mavlink_command_int_t &packet)
{
    const bool change_modes = ((int32_t)packet.param2 & MAV_DO_REPOSITION_FLAGS_CHANGE_MODE) == MAV_DO_REPOSITION_FLAGS_CHANGE_MODE;
    if (!rover.control_mode->in_guided_mode() && !change_modes) {
        return MAV_RESULT_DENIED;
    }

    // sanity check location
    if (!check_latlng(packet.x, packet.y)) {
        return MAV_RESULT_DENIED;
    }
    if (packet.x == 0 && packet.y == 0) {
        return MAV_RESULT_DENIED;
    }

    Location request_location {};
    request_location.lat = packet.x;
    request_location.lng = packet.y;

    if (fabsf(packet.z) > LOCATION_ALT_MAX_M) {
        return MAV_RESULT_DENIED;
    }

    Location::AltFrame frame;
    if (!mavlink_coordinate_frame_to_location_alt_frame((MAV_FRAME)packet.frame, frame)) {
        return MAV_RESULT_DENIED; // failed as the location is not valid
    }
    request_location.set_alt_cm((int32_t)(packet.z * 100.0f), frame);

    if (!rover.control_mode->in_guided_mode()) {
        if (!rover.set_mode(Mode::Number::GUIDED, ModeReason::GCS_COMMAND)) {
            return MAV_RESULT_FAILED;
        }
    }

    if (is_positive(packet.param1)) {
        if (!rover.control_mode->set_desired_speed(packet.param1)) {
            return MAV_RESULT_FAILED;
        }
    }

    // set the destination
    if (!rover.mode_guided.set_desired_location(request_location)) {
        return MAV_RESULT_FAILED;
    }

    return MAV_RESULT_ACCEPTED;
}

void GCS_MAVLINK_Rover::handleMessage(const mavlink_message_t &msg)
{
    switch (msg.msgid) {
    case MAVLINK_MSG_ID_MANUAL_CONTROL:
        handle_manual_control(msg);
        break;

    case MAVLINK_MSG_ID_SET_ATTITUDE_TARGET:
        handle_set_attitude_target(msg);
        break;

    case MAVLINK_MSG_ID_SET_POSITION_TARGET_LOCAL_NED:
        handle_set_position_target_local_ned(msg);
        break;

    case MAVLINK_MSG_ID_SET_POSITION_TARGET_GLOBAL_INT:
        handle_set_position_target_global_int(msg);
        break;

    case MAVLINK_MSG_ID_RADIO:
    case MAVLINK_MSG_ID_RADIO_STATUS:
        handle_radio(msg);
        break;

    default:
        handle_common_message(msg);
        break;
    }
}

void GCS_MAVLINK_Rover::handle_manual_control(const mavlink_message_t &msg)
{
    if (msg.sysid != rover.g.sysid_my_gcs) {  // Only accept control from our gcs
        return;
    }

    mavlink_manual_control_t packet;
    mavlink_msg_manual_control_decode(&msg, &packet);

    if (packet.target != rover.g.sysid_this_mav) {
        return; // only accept control aimed at us
    }

    uint32_t tnow = AP_HAL::millis();

    manual_override(rover.channel_steer, packet.y, 1000, 2000, tnow);
    manual_override(rover.channel_throttle, packet.z, 1000, 2000, tnow);

    // a manual control message is considered to be a 'heartbeat' from
    // the ground station for failsafe purposes
    gcs().sysid_myggcs_seen(tnow);
}

void GCS_MAVLINK_Rover::handle_set_attitude_target(const mavlink_message_t &msg)
{
    // decode packet
    mavlink_set_attitude_target_t packet;
    mavlink_msg_set_attitude_target_decode(&msg, &packet);

    // exit if vehicle is not in Guided mode
    if (!rover.control_mode->in_guided_mode()) {
        return;
    }

    // ensure type_mask specifies to use thrust
    if ((packet.type_mask & MAVLINK_SET_ATT_TYPE_MASK_THROTTLE_IGNORE) != 0) {
        return;
    }

    // convert thrust to ground speed
    packet.thrust = constrain_float(packet.thrust, -1.0f, 1.0f);
    const float target_speed = rover.control_mode->get_speed_default() * packet.thrust;

    // if the body_yaw_rate field is ignored, convert quaternion to heading
    if ((packet.type_mask & MAVLINK_SET_ATT_TYPE_MASK_YAW_RATE_IGNORE) != 0) {
        // convert quaternion to heading
        float target_heading_cd = degrees(Quaternion(packet.q[0], packet.q[1], packet.q[2], packet.q[3]).get_euler_yaw()) * 100.0f;
        rover.mode_guided.set_desired_heading_and_speed(target_heading_cd, target_speed);
    } else {
        // use body_yaw_rate field
        rover.mode_guided.set_desired_turn_rate_and_speed((RAD_TO_DEG * packet.body_yaw_rate) * 100.0f, target_speed);
    }
}

void GCS_MAVLINK_Rover::handle_set_position_target_local_ned(const mavlink_message_t &msg)
{
    // decode packet
    mavlink_set_position_target_local_ned_t packet;
    mavlink_msg_set_position_target_local_ned_decode(&msg, &packet);

    // exit if vehicle is not in Guided mode
    if (!rover.control_mode->in_guided_mode()) {
        return;
    }

    // need ekf origin
    Location ekf_origin;
    if (!rover.ahrs.get_origin(ekf_origin)) {
        return;
    }

    // check for supported coordinate frames
    if (packet.coordinate_frame != MAV_FRAME_LOCAL_NED &&
        packet.coordinate_frame != MAV_FRAME_LOCAL_OFFSET_NED &&
        packet.coordinate_frame != MAV_FRAME_BODY_NED &&
        packet.coordinate_frame != MAV_FRAME_BODY_OFFSET_NED) {
        return;
    }

    bool pos_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_POS_IGNORE;
    bool vel_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_VEL_IGNORE;
    bool acc_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_ACC_IGNORE;
    bool yaw_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_YAW_IGNORE;
    bool yaw_rate_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_YAW_RATE_IGNORE;

    // prepare target position
    Location target_loc = rover.current_loc;
    if (!pos_ignore) {
        switch (packet.coordinate_frame) {
        case MAV_FRAME_BODY_NED:
        case MAV_FRAME_BODY_OFFSET_NED: {
            // rotate from body-frame to NE frame
            const float ne_x = packet.x * rover.ahrs.cos_yaw() - packet.y * rover.ahrs.sin_yaw();
            const float ne_y = packet.x * rover.ahrs.sin_yaw() + packet.y * rover.ahrs.cos_yaw();
            // add offset to current location
            target_loc.offset(ne_x, ne_y);
        }
            break;

        case MAV_FRAME_LOCAL_OFFSET_NED:
            // add offset to current location
            target_loc.offset(packet.x, packet.y);
            break;

        case MAV_FRAME_LOCAL_NED:
        default:
            // MAV_FRAME_LOCAL_NED is interpreted as an offset from EKF origin
            target_loc = ekf_origin;
            target_loc.offset(packet.x, packet.y);
            break;
        }
    }

    float target_speed = 0.0f;
    float target_yaw_cd = 0.0f;

    // consume velocity and convert to target speed and heading
    if (!vel_ignore) {
        const float speed_max = rover.control_mode->get_speed_default();
        // convert vector length into a speed
        target_speed = constrain_float(safe_sqrt(sq(packet.vx) + sq(packet.vy)), -speed_max, speed_max);
        // convert vector direction to target yaw
        target_yaw_cd = degrees(atan2f(packet.vy, packet.vx)) * 100.0f;

        // rotate target yaw if provided in body-frame
        if (packet.coordinate_frame == MAV_FRAME_BODY_NED || packet.coordinate_frame == MAV_FRAME_BODY_OFFSET_NED) {
            target_yaw_cd = wrap_180_cd(target_yaw_cd + rover.ahrs.yaw_sensor);
        }
    }

    // consume yaw heading
    if (!yaw_ignore) {
        target_yaw_cd = ToDeg(packet.yaw) * 100.0f;
        // rotate target yaw if provided in body-frame
        if (packet.coordinate_frame == MAV_FRAME_BODY_NED || packet.coordinate_frame == MAV_FRAME_BODY_OFFSET_NED) {
            target_yaw_cd = wrap_180_cd(target_yaw_cd + rover.ahrs.yaw_sensor);
        }
    }
    // consume yaw rate
    float target_turn_rate_cds = 0.0f;
    if (!yaw_rate_ignore) {
        target_turn_rate_cds = ToDeg(packet.yaw_rate) * 100.0f;
    }

    // handling case when both velocity and either yaw or yaw-rate are provided
    // by default, we consider that the rover will drive forward
    float speed_dir = 1.0f;
    if (!vel_ignore && (!yaw_ignore || !yaw_rate_ignore)) {
        // Note: we are using the x-axis velocity to determine direction even though
        // the frame may have been provided in MAV_FRAME_LOCAL_OFFSET_NED or MAV_FRAME_LOCAL_NED
        if (is_negative(packet.vx)) {
            speed_dir = -1.0f;
        }
    }

    // set guided mode targets
    if (!pos_ignore) {
        // consume position target
        if (!rover.mode_guided.set_desired_location(target_loc)) {
            // GCS will need to monitor desired location to
            // see if they are having an effect.
        }
    } else if (!vel_ignore && acc_ignore && yaw_ignore && yaw_rate_ignore) {
        // consume velocity
        rover.mode_guided.set_desired_heading_and_speed(target_yaw_cd, speed_dir * target_speed);
    } else if (!vel_ignore && acc_ignore && yaw_ignore && !yaw_rate_ignore) {
        // consume velocity and turn rate
        rover.mode_guided.set_desired_turn_rate_and_speed(target_turn_rate_cds, speed_dir * target_speed);
    } else if (!vel_ignore && acc_ignore && !yaw_ignore && yaw_rate_ignore) {
        // consume velocity and heading
        rover.mode_guided.set_desired_heading_and_speed(target_yaw_cd, speed_dir * target_speed);
    } else if (vel_ignore && acc_ignore && !yaw_ignore && yaw_rate_ignore) {
        // consume just target heading (probably only skid steering vehicles can do this)
        rover.mode_guided.set_desired_heading_and_speed(target_yaw_cd, 0.0f);
    } else if (vel_ignore && acc_ignore && yaw_ignore && !yaw_rate_ignore) {
        // consume just turn rate (probably only skid steering vehicles can do this)
        rover.mode_guided.set_desired_turn_rate_and_speed(target_turn_rate_cds, 0.0f);
    }
}

void GCS_MAVLINK_Rover::handle_set_position_target_global_int(const mavlink_message_t &msg)
{
    // decode packet
    mavlink_set_position_target_global_int_t packet;
    mavlink_msg_set_position_target_global_int_decode(&msg, &packet);

    // exit if vehicle is not in Guided mode
    if (!rover.control_mode->in_guided_mode()) {
        return;
    }
    // check for supported coordinate frames
    if (packet.coordinate_frame != MAV_FRAME_GLOBAL &&
        packet.coordinate_frame != MAV_FRAME_GLOBAL_INT &&
        packet.coordinate_frame != MAV_FRAME_GLOBAL_RELATIVE_ALT &&
        packet.coordinate_frame != MAV_FRAME_GLOBAL_RELATIVE_ALT_INT &&
        packet.coordinate_frame != MAV_FRAME_GLOBAL_TERRAIN_ALT &&
        packet.coordinate_frame != MAV_FRAME_GLOBAL_TERRAIN_ALT_INT) {
        return;
    }
    bool pos_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_POS_IGNORE;
    bool vel_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_VEL_IGNORE;
    bool acc_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_ACC_IGNORE;
    bool yaw_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_YAW_IGNORE;
    bool yaw_rate_ignore = packet.type_mask & MAVLINK_SET_POS_TYPE_MASK_YAW_RATE_IGNORE;

    // prepare target position
    Location target_loc = rover.current_loc;
    if (!pos_ignore) {
        // sanity check location
        if (!check_latlng(packet.lat_int, packet.lon_int)) {
            // result = MAV_RESULT_FAILED;
            return;
        }
        target_loc.lat = packet.lat_int;
        target_loc.lng = packet.lon_int;
    }

    float target_speed = 0.0f;
    float target_yaw_cd = 0.0f;

    // consume velocity and convert to target speed and heading
    if (!vel_ignore) {
        const float speed_max = rover.control_mode->get_speed_default();
        // convert vector length into a speed
        target_speed = constrain_float(safe_sqrt(sq(packet.vx) + sq(packet.vy)), -speed_max, speed_max);
        // convert vector direction to target yaw
        target_yaw_cd = degrees(atan2f(packet.vy, packet.vx)) * 100.0f;
    }

    // consume yaw heading
    if (!yaw_ignore) {
        target_yaw_cd = ToDeg(packet.yaw) * 100.0f;
    }

    // consume yaw rate
    float target_turn_rate_cds = 0.0f;
    if (!yaw_rate_ignore) {
        target_turn_rate_cds = ToDeg(packet.yaw_rate) * 100.0f;
    }

    // handling case when both velocity and either yaw or yaw-rate are provided
    // by default, we consider that the rover will drive forward
    float speed_dir = 1.0f;
    if (!vel_ignore && (!yaw_ignore || !yaw_rate_ignore)) {
        // Note: we are using the x-axis velocity to determine direction even though
        // the frame is provided in MAV_FRAME_GLOBAL_xxx
        if (is_negative(packet.vx)) {
            speed_dir = -1.0f;
        }
    }

    // set guided mode targets
    if (!pos_ignore) {
        // consume position target
        if (!rover.mode_guided.set_desired_location(target_loc)) {
            // GCS will just need to look at desired location
            // outputs to see if it having an effect.
        }
    } else if (!vel_ignore && acc_ignore && yaw_ignore && yaw_rate_ignore) {
        // consume velocity
        rover.mode_guided.set_desired_heading_and_speed(target_yaw_cd, speed_dir * target_speed);
    } else if (!vel_ignore && acc_ignore && yaw_ignore && !yaw_rate_ignore) {
        // consume velocity and turn rate
        rover.mode_guided.set_desired_turn_rate_and_speed(target_turn_rate_cds, speed_dir * target_speed);
    } else if (!vel_ignore && acc_ignore && !yaw_ignore && yaw_rate_ignore) {
        // consume velocity
        rover.mode_guided.set_desired_heading_and_speed(target_yaw_cd, speed_dir * target_speed);
    } else if (vel_ignore && acc_ignore && !yaw_ignore && yaw_rate_ignore) {
        // consume just target heading (probably only skid steering vehicles can do this)
        rover.mode_guided.set_desired_heading_and_speed(target_yaw_cd, 0.0f);
    } else if (vel_ignore && acc_ignore && yaw_ignore && !yaw_rate_ignore) {
        // consume just turn rate(probably only skid steering vehicles can do this)
        rover.mode_guided.set_desired_turn_rate_and_speed(target_turn_rate_cds, 0.0f);
    }
}

void GCS_MAVLINK_Rover::handle_radio(const mavlink_message_t &msg)
{
    handle_radio_status(msg, rover.should_log(MASK_LOG_PM));
}


uint64_t GCS_MAVLINK_Rover::capabilities() const
{
    return (MAV_PROTOCOL_CAPABILITY_MISSION_FLOAT |
            MAV_PROTOCOL_CAPABILITY_MISSION_INT |
            MAV_PROTOCOL_CAPABILITY_COMMAND_INT |
            MAV_PROTOCOL_CAPABILITY_SET_POSITION_TARGET_LOCAL_NED |
            MAV_PROTOCOL_CAPABILITY_SET_POSITION_TARGET_GLOBAL_INT |
            MAV_PROTOCOL_CAPABILITY_SET_ATTITUDE_TARGET |
            GCS_MAVLINK::capabilities());
}

#if HAL_HIGH_LATENCY2_ENABLED
uint8_t GCS_MAVLINK_Rover::high_latency_tgt_heading() const
{
    const Mode *control_mode = rover.control_mode;
    if (rover.control_mode->is_autopilot_mode()) {
        // need to convert -180->180 to 0->360/2
        return wrap_360(control_mode->wp_bearing()) / 2;
    }
    return 0;      
}
    
uint16_t GCS_MAVLINK_Rover::high_latency_tgt_dist() const
{
    const Mode *control_mode = rover.control_mode;
    if (rover.control_mode->is_autopilot_mode()) {
        // return units are dm
        return MIN((control_mode->get_distance_to_destination()) / 10, UINT16_MAX);
    }
    return 0;  
}

uint8_t GCS_MAVLINK_Rover::high_latency_tgt_airspeed() const
{
    const Mode *control_mode = rover.control_mode;
    if (rover.control_mode->is_autopilot_mode()) {
        // return units are m/s*5
        return MIN((vfr_hud_airspeed() - control_mode->speed_error()) * 5, UINT8_MAX);
    }
    return 0;
}

uint8_t GCS_MAVLINK_Rover::high_latency_wind_speed() const
{
    if (rover.g2.windvane.enabled()) {
        // return units are m/s*5
        return MIN(rover.g2.windvane.get_true_wind_speed() * 5, UINT8_MAX);
    }
    return 0; 
}

uint8_t GCS_MAVLINK_Rover::high_latency_wind_direction() const
{
    if (rover.g2.windvane.enabled()) {
        // return units are deg/2
        return wrap_360(degrees(rover.g2.windvane.get_true_wind_direction_rad())) / 2;
    }
    return 0; 
}
#endif // HAL_HIGH_LATENCY2_ENABLED
