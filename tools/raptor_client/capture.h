// capture — the reply recorder for raptor_client (row #140).
//
// A capture bundle is a directory OUTSIDE the repository:
//
//   <dir>/replies/port-<port>.bin   one file per port, length-prefixed records
//   <dir>/manifest.json             the run's identity and every file's hash
//
// The bundle is evidence for the hardware room and is never committed, so
// the recorder refuses a directory inside a git repository.
//
// The record format (little-endian throughout):
//
//   file header (12 bytes)
//     magic     4 bytes  "RCRP"
//     version   u32      = 1
//     port      u16      the port this file carries
//     reserved  u16      = 0
//
//   records, repeated to the end of the file
//     body_len  u32      the length of the body that follows
//     body
//       ts_ms      u64    wall clock, milliseconds since the Unix epoch
//       port       u16
//       method_len u16
//       method     method_len bytes
//       msgid      u64
//       byte_len   u64
//       bytes      byte_len bytes, the reply as it arrived
//
// The body is exactly the tuple the row asks for — ts, port, method, msgid,
// byte length, bytes — and the length prefix makes the file self-delimiting.

#ifndef OPENSCANNER_RAPTOR_CLIENT_CAPTURE_H
#define OPENSCANNER_RAPTOR_CLIENT_CAPTURE_H

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace capture {

// One captured reply.
struct Record {
    uint64_t ts_ms = 0;  // wall clock, milliseconds since the Unix epoch
    uint16_t port = 0;
    std::string method;
    uint64_t msgid = 0;
    std::vector<uint8_t> bytes;  // the reply, raw and unparsed
};

// SHA-256 (FIPS 180-4). The manifest's file hashes.
class Sha256 {
public:
    Sha256() { Reset(); }
    void Reset();
    void Update(const uint8_t* data, size_t n);
    void Update(const std::string& s);
    // The digest as 64 lowercase hex characters. Finalises the object.
    std::string HexDigest();

private:
    void Block(const uint8_t* p);

    uint32_t h_[8];
    uint8_t buf_[64];
    size_t buf_len_ = 0;
    uint64_t total_ = 0;
};

// The hex sha256 of a file, or "" when the file cannot be read.
std::string FileSha256(const std::string& path);

// The hex sha256 of a byte range.
std::string BytesSha256(const uint8_t* data, size_t n);

// The recorder. One bundle per run.
class Recorder {
public:
    Recorder();
    ~Recorder();

    // Open the bundle. Fails when the directory is inside a git repository
    // or cannot be created; *err then says which.
    bool Open(const std::string& dir, const std::string& binary_path, int seconds_cap, uint64_t bytes_cap,
              std::string* err);

    // Record one reply. Ignored once a cap is hit.
    void Record(uint16_t port, const std::string& method, uint64_t msgid, const std::vector<uint8_t>& bytes);

    // True once a cap stopped the recording.
    bool Capped() const { return capped_; }
    bool SecondsCapHit() const { return capped_seconds_; }
    bool BytesCapHit() const { return capped_bytes_; }
    uint64_t RecordCount() const { return records_; }
    uint64_t BytesWritten() const { return bytes_written_; }
    const std::string& dir() const { return dir_; }

    // Close the files and write the manifest. Returns the manifest's path.
    std::string Close(const std::string& branch, const std::string& commit);

private:
    struct PortFile {
        std::unique_ptr<std::ofstream> out;
        uint64_t records = 0;
    };

    std::string dir_;
    std::string binary_path_;
    int seconds_cap_ = 0;
    uint64_t bytes_cap_ = 0;
    uint64_t start_ms_ = 0;
    uint64_t end_ms_ = 0;
    uint64_t records_ = 0;
    uint64_t bytes_written_ = 0;
    bool capped_ = false;
    bool capped_seconds_ = false;
    bool capped_bytes_ = false;
    bool open_ = false;
    // Two connections can share a port (the replay's per-interface routing
    // may point both at one port), so the writes are serialised.
    std::mutex mutex_;
    std::map<uint16_t, PortFile> files_;
};

// The reader: the format's other half. It is what makes the round trip
// checkable, and what lets a bundle be read without a second parser.
class Reader {
public:
    // Open a bundle's replies directory.
    bool Open(const std::string& dir, std::string* err);

    // The next record; false at the end of the file.
    bool Next(Record* out);

    // The files the reader found, in name order.
    const std::vector<std::string>& files() const { return files_; }
    // The last error, if any.
    const std::string& error() const { return error_; }

private:
    bool OpenFile(const std::string& path);
    bool OpenNext();

    std::string dir_;
    std::vector<std::string> files_;
    size_t file_index_ = 0;
    std::ifstream in_;
    uint16_t port_ = 0;
    bool done_ = false;
    std::string error_;
};

// The branch and the commit of the repository the given directory is in, or
// "unknown" when there is none. Read from .git — no subprocess.
void ReadGitBranchAndCommit(const std::string& start_dir, std::string* branch, std::string* commit);

// True when the path is inside a git repository.
bool IsInsideRepo(const std::string& path);

// Remove a directory tree. Used by the selftest to clean up its bundle.
void RemoveTree(const std::string& path);

// The wall clock in milliseconds since the Unix epoch.
uint64_t NowMs();

// The wall clock as an ISO-8601 UTC stamp.
std::string UtcStamp(uint64_t ms);

}  // namespace capture

#endif  // OPENSCANNER_RAPTOR_CLIENT_CAPTURE_H
