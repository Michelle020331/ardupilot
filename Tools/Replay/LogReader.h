#pragma once

#include "VehicleType.h"
#include "DataFlashFileReader.h"
#include "LR_MsgHandler.h"
#include "Parameters.h"

class LogReader : public AP_LoggerFileReader
{
public:
    LogReader(struct LogStructure *log_structure, NavEKF2 &_ekf, NavEKF3 &_ekf3);

    const Vector3f &get_attitude(void) const { return attitude; }
    const Vector3f &get_ahr2_attitude(void) const { return ahr2_attitude; }
    const Vector3f &get_inavpos(void) const { return inavpos; }
    const Vector3f &get_sim_attitude(void) const { return sim_attitude; }
    const float &get_relalt(void) const { return rel_altitude; }
    const LR_MsgHandler::CheckState &get_check_state(void) const { return check_state; }

    VehicleType::vehicle_type vehicle;

    bool check_user_param(const char *name);
    bool set_parameter(const char *name, float value, bool force=false);

    void set_accel_mask(uint8_t mask) { accel_mask = mask; }
    void set_gyro_mask(uint8_t mask) { gyro_mask = mask; }
    void set_use_imt(bool _use_imt) { use_imt = _use_imt; }
    void set_save_chek_messages(bool _save_chek_messages) { save_chek_messages = _save_chek_messages; }

    uint64_t last_timestamp_us(void) const { return last_timestamp_usec; }
    bool handle_log_format_msg(const struct log_Format &f) override;
    bool handle_msg(const struct log_Format &f, uint8_t *msg) override;

    static bool in_list(const char *type, const char *list[]);

protected:

private:

    NavEKF2 &ekf2;
    NavEKF3 &ekf3;

    struct LogStructure *_log_structure;
    uint8_t _log_structure_count;

    uint8_t accel_mask;
    uint8_t gyro_mask;
    bool use_imt = true;

    uint32_t ground_alt_cm;

    class LR_MsgHandler *msgparser[LOGREADER_MAX_FORMATS] {};

    Vector3f attitude;
    Vector3f ahr2_attitude;
    Vector3f sim_attitude;
    Vector3f inavpos;
    float rel_altitude;
    uint64_t last_timestamp_usec;

    // mapping from original msgid to output msgid
    uint8_t mapped_msgid[256] {};

    // next available msgid for mapping
    uint8_t next_msgid = 1;

    LR_MsgHandler::CheckState check_state;

    bool installed_vehicle_specific_parsers;

    bool save_chek_messages;

    void maybe_install_vehicle_specific_parsers();

    void initialise_fmt_map();
    uint8_t map_fmt_type(const char *name, uint8_t intype);

    bool save_message_type(const char *name);
};

// some vars are difficult to get through the layers
struct globals {
    bool no_params;
};

extern struct globals globals;
