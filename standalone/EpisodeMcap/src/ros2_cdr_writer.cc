#include "episode_mcap/ros2_cdr_writer.h"

#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>

#include <algorithm>
#include <cstring>

namespace ugripper::episode_mcap {
namespace {

class CdrWriter {
public:
    CdrWriter() {
        data_.push_back(std::byte{0x00});
        data_.push_back(std::byte{0x01});
        data_.push_back(std::byte{0x00});
        data_.push_back(std::byte{0x00});
    }

    void Align(size_t alignment) {
        const size_t relative = data_.size() - 4;
        const size_t rem = relative % alignment;
        if (rem != 0) {
            data_.insert(data_.end(), alignment - rem, std::byte{0});
        }
    }

    void U8(uint8_t value) {
        data_.push_back(static_cast<std::byte>(value));
    }

    void Bool(bool value) {
        U8(value ? 1 : 0);
    }

    void I32(int32_t value) {
        WritePod(value);
    }

    void U32(uint32_t value) {
        WritePod(value);
    }

    void F32(float value) {
        WritePod(value);
    }

    void F64(double value) {
        WritePod(value);
    }

    void String(const std::string& value) {
        U32(static_cast<uint32_t>(value.size() + 1));
        const auto* bytes = reinterpret_cast<const std::byte*>(value.data());
        data_.insert(data_.end(), bytes, bytes + value.size());
        data_.push_back(std::byte{0});
    }

    void Bytes(const uint8_t* data, size_t size) {
        U32(static_cast<uint32_t>(size));
        const auto* bytes = reinterpret_cast<const std::byte*>(data);
        data_.insert(data_.end(), bytes, bytes + size);
    }

    void Header(uint64_t stamp_ns, const std::string& frame_id) {
        const RosTime time = RosTimeFromUnixNs(stamp_ns);
        I32(time.sec);
        U32(time.nanosec);
        String(frame_id);
    }

    std::vector<std::byte> Take() {
        return std::move(data_);
    }

private:
    template <typename T>
    void WritePod(T value) {
        Align(sizeof(T));
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        data_.insert(data_.end(), bytes, bytes + sizeof(T));
    }

    std::vector<std::byte> data_;
};

std::vector<double> FixedVector(const std::vector<double>& input, size_t size, double diagonal = 0.0) {
    std::vector<double> values(size, 0.0);
    if (!input.empty()) {
        std::copy(input.begin(), input.begin() + std::min(input.size(), size), values.begin());
        return values;
    }
    if (size == 9) {
        values[0] = diagonal;
        values[4] = diagonal;
        values[8] = diagonal;
    } else if (size == 12) {
        values[0] = diagonal;
        values[5] = diagonal;
        values[10] = diagonal;
    }
    return values;
}

}  // namespace

RosTime RosTimeFromUnixNs(uint64_t unix_time_ns) {
    RosTime time;
    time.sec = static_cast<int32_t>(unix_time_ns / 1000000000ULL);
    time.nanosec = static_cast<uint32_t>(unix_time_ns % 1000000000ULL);
    return time;
}

std::vector<std::byte> EncodeCompressedImage(uint64_t stamp_ns,
                                             const std::string& frame_id,
                                             const std::string& format,
                                             const uint8_t* data,
                                             size_t data_size) {
    CdrWriter writer;
    writer.Header(stamp_ns, frame_id);
    writer.String(format);
    writer.Bytes(data, data_size);
    return writer.Take();
}

std::vector<std::byte> EncodeCameraInfo(uint64_t stamp_ns,
                                        const std::string& frame_id,
                                        uint32_t width,
                                        uint32_t height,
                                        const std::string& distortion_model,
                                        const std::vector<double>& d,
                                        const std::vector<double>& k,
                                        const std::vector<double>& r,
                                        const std::vector<double>& p) {
    CdrWriter writer;
    writer.Header(stamp_ns, frame_id);
    writer.U32(height);
    writer.U32(width);
    writer.String(distortion_model);
    writer.U32(static_cast<uint32_t>(d.size()));
    for (double value : d) {
        writer.F64(value);
    }
    for (double value : FixedVector(k, 9, 1.0)) {
        writer.F64(value);
    }
    for (double value : FixedVector(r, 9, 1.0)) {
        writer.F64(value);
    }
    for (double value : FixedVector(p, 12, 1.0)) {
        writer.F64(value);
    }
    writer.U32(0);
    writer.U32(0);
    writer.U32(0);
    writer.U32(0);
    writer.U32(0);
    writer.U32(0);
    writer.Bool(false);
    return writer.Take();
}

std::vector<std::byte> EncodeImu(uint64_t stamp_ns,
                                 const std::string& frame_id,
                                 double gx,
                                 double gy,
                                 double gz,
                                 double ax,
                                 double ay,
                                 double az) {
    CdrWriter writer;
    writer.Header(stamp_ns, frame_id);
    writer.F64(0.0);
    writer.F64(0.0);
    writer.F64(0.0);
    writer.F64(1.0);
    writer.F64(-1.0);
    for (int i = 1; i < 9; ++i) writer.F64(0.0);
    writer.F64(gx);
    writer.F64(gy);
    writer.F64(gz);
    for (int i = 0; i < 9; ++i) writer.F64(0.0);
    writer.F64(ax);
    writer.F64(ay);
    writer.F64(az);
    for (int i = 0; i < 9; ++i) writer.F64(0.0);
    return writer.Take();
}

std::vector<std::byte> EncodeEncoder(uint64_t stamp_ns,
                                     int32_t raw,
                                     float rad) {
    CdrWriter writer;
    writer.Header(stamp_ns, "");
    writer.I32(raw);
    writer.F32(rad);
    return writer.Take();
}

std::vector<std::byte> EncodeString(const std::string& value) {
    std::vector<char> buffer(4 + 4 + value.size() + 1);
    eprosima::fastcdr::FastBuffer fast_buffer(buffer.data(), buffer.size());
    eprosima::fastcdr::Cdr serializer(
        fast_buffer,
        eprosima::fastcdr::Cdr::LITTLE_ENDIANNESS,
        eprosima::fastcdr::Cdr::DDS_CDR);
    serializer.serialize_encapsulation();
    serializer << value;

    const size_t size = serializer.getSerializedDataLength();
    std::vector<std::byte> data(size);
    std::memcpy(data.data(), buffer.data(), size);
    return data;
}

const char* CompressedImageSchema() {
    return R"(std_msgs/Header header
string format
uint8[] data
================================================================================
MSG: std_msgs/Header
builtin_interfaces/Time stamp
string frame_id
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec)";
}

const char* CameraInfoSchema() {
    return R"(std_msgs/Header header
uint32 height
uint32 width
string distortion_model
float64[] d
float64[9] k
float64[9] r
float64[12] p
uint32 binning_x
uint32 binning_y
sensor_msgs/RegionOfInterest roi
================================================================================
MSG: std_msgs/Header
builtin_interfaces/Time stamp
string frame_id
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec
================================================================================
MSG: sensor_msgs/RegionOfInterest
uint32 x_offset
uint32 y_offset
uint32 height
uint32 width
bool do_rectify)";
}

const char* ImuSchema() {
    return R"(std_msgs/Header header
geometry_msgs/Quaternion orientation
float64[9] orientation_covariance
geometry_msgs/Vector3 angular_velocity
float64[9] angular_velocity_covariance
geometry_msgs/Vector3 linear_acceleration
float64[9] linear_acceleration_covariance
================================================================================
MSG: std_msgs/Header
builtin_interfaces/Time stamp
string frame_id
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec
================================================================================
MSG: geometry_msgs/Quaternion
float64 x
float64 y
float64 z
float64 w
================================================================================
MSG: geometry_msgs/Vector3
float64 x
float64 y
float64 z)";
}

const char* EncoderSchema() {
    return R"(std_msgs/Header header
int32 raw
float32 rad
================================================================================
MSG: std_msgs/Header
builtin_interfaces/Time stamp
string frame_id
================================================================================
MSG: builtin_interfaces/Time
int32 sec
uint32 nanosec)";
}

const char* StringSchema() {
    return "string data";
}

}  // namespace ugripper::episode_mcap
