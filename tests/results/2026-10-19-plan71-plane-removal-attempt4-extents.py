import struct, sys

def main(path):
    with open(path, 'rb') as f:
        data = f.read()
    off = 0
    magic = data[0:4]
    assert magic == b'OSCR', magic
    version = struct.unpack_from('<I', data, 4)[0]
    off = 16
    minx=miny=minz=float('inf')
    maxx=maxy=maxz=float('-inf')
    frame_count = 0
    cloud_frames = 0
    total_points = 0
    max_points_in_frame = 0
    while off + 32 <= len(data):
        magic2, version2, mtype, seq = struct.unpack_from('<IHHI', data, off)
        tcap, tserv = struct.unpack_from('<dd', data, off+8)
        payload_len = struct.unpack_from('<I', data, off+28)[0]
        if magic2 != 0x4F534346:
            print(f"bad magic at off {off}")
            break
        frame_start = off + 32
        if frame_start + payload_len > len(data):
            print("truncated frame, stopping")
            break
        payload = data[frame_start:frame_start+payload_len]
        frame_count += 1
        if mtype == 3:  # CloudSnapshot
            count = struct.unpack_from('<I', payload, 0)[0]
            cloud_frames += 1
            total_points += count
            max_points_in_frame = max(max_points_in_frame, count)
            xyz_bytes = payload[4:4+count*12]
            if count > 0:
                xs = struct.unpack_from(f'<{count}f', xyz_bytes, 0*4)  # placeholder
        off = frame_start + payload_len

    # second pass just for extents (simpler, redo cleanly)
    off = 16
    while off + 32 <= len(data):
        magic2, version2, mtype, seq = struct.unpack_from('<IHHI', data, off)
        tcap, tserv = struct.unpack_from('<dd', data, off+8)
        payload_len = struct.unpack_from('<I', data, off+28)[0]
        frame_start = off + 32
        if frame_start + payload_len > len(data):
            break
        payload = data[frame_start:frame_start+payload_len]
        if mtype == 3:
            count = struct.unpack_from('<I', payload, 0)[0]
            if count > 0:
                arr = struct.unpack_from(f'<{count*3}f', payload, 4)
                for i in range(count):
                    x, y, z = arr[i*3], arr[i*3+1], arr[i*3+2]
                    if x < minx: minx = x
                    if x > maxx: maxx = x
                    if y < miny: miny = y
                    if y > maxy: maxy = y
                    if z < minz: minz = z
                    if z > maxz: maxz = z
        off = frame_start + payload_len

    print(f"frames={frame_count} cloud_frames={cloud_frames} total_points_summed={total_points} max_points_in_single_frame={max_points_in_frame}")
    print(f"x: [{minx}, {maxx}]")
    print(f"y: [{miny}, {maxy}]")
    print(f"z: [{minz}, {maxz}]")

if __name__ == '__main__':
    main(sys.argv[1])
