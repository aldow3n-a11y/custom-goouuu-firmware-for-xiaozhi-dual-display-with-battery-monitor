#!/usr/bin/env python3
"""Generate idle background GIF animations for 128x160 TFT display."""

import struct
import os
import math
import random

WIDTH = 128
HEIGHT = 160
NUM_FRAMES = 10
FRAME_DELAY = 100  # ms per frame (10 fps)


def create_palette(colors):
    """Create a 256-color palette from list of (r,g,b) tuples."""
    palette = [(0, 0, 0)] * 256
    for i, c in enumerate(colors[:256]):
        palette[i] = c
    return palette


def quantize_color(r, g, b, palette):
    """Find closest palette color."""
    best_idx = 0
    best_dist = float('inf')
    for i, (pr, pg, pb) in enumerate(palette):
        dist = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if dist < best_dist:
            best_dist = dist
            best_idx = i
            if dist == 0:
                break
    return best_idx


def write_gif(filename, frames, palette, width, height, delay_ms=100):
    """Write animated GIF file."""
    delay_centiseconds = delay_ms // 10

    with open(filename, 'wb') as f:
        # GIF89a header
        f.write(b'GIF89a')

        # Logical Screen Descriptor
        f.write(struct.pack('<HH', width, height))
        # Global Color Table: 1 flag byte (256 colors = 7), no bg, no aspect
        f.write(bytes([0xF7, 0x00, 0x00]))

        # Global Color Table (256 * 3 bytes)
        for r, g, b in palette:
            f.write(bytes([r, g, b]))

        # Netscape looping extension (infinite loop)
        f.write(bytes([0x21, 0xFF, 0x0B]))
        f.write(b'NETSCAPE2.0')
        f.write(bytes([0x03, 0x01]))
        f.write(struct.pack('<H', 0))  # loop count 0 = infinite
        f.write(bytes([0x00]))

        for frame_idx, frame_data in enumerate(frames):
            # Graphic Control Extension
            f.write(bytes([0x21, 0xF9, 0x04]))
            # disposal: restore to bg (2), transparent: no, delay: yes
            f.write(bytes([0x04, 0x00]))
            f.write(struct.pack('<H', delay_centiseconds))
            f.write(bytes([0x00]))  # transparent color index (unused)
            f.write(bytes([0x00]))  # block terminator

            # Image Descriptor
            f.write(bytes([0x2C]))
            f.write(struct.pack('<HH', 0, 0))  # left, top
            f.write(struct.pack('<HH', width, height))
            f.write(bytes([0x00]))  # no local color table

            # LZW Minimum Code Size
            min_code_size = 8
            f.write(bytes([min_code_size]))

            # LZW compress the frame data
            compressed = lzw_compress(frame_data, min_code_size)

            # Write sub-blocks
            i = 0
            while i < len(compressed):
                chunk_size = min(255, len(compressed) - i)
                f.write(bytes([chunk_size]))
                f.write(compressed[i:i + chunk_size])
                i += chunk_size
            f.write(bytes([0x00]))  # block terminator

        # GIF Trailer
        f.write(bytes([0x3B]))


def lzw_compress(data, min_code_size):
    """LZW compress data for GIF."""
    clear_code = 1 << min_code_size
    eoi_code = clear_code + 1

    # Initialize dictionary
    code_size = min_code_size + 1
    next_code = eoi_code + 1
    dictionary = {}
    for i in range(clear_code):
        dictionary[(i,)] = i

    result = []
    buffer = 0
    bits_in_buffer = 0

    def emit(code):
        nonlocal buffer, bits_in_buffer
        buffer |= (code << bits_in_buffer)
        bits_in_buffer += code_size
        while bits_in_buffer >= 8:
            result.append(buffer & 0xFF)
            buffer >>= 8
            bits_in_buffer -= 8

    emit(clear_code)

    w = (data[0],)
    for k in data[1:]:
        wk = w + (k,)
        if wk in dictionary:
            w = wk
        else:
            emit(dictionary[w])
            if next_code < 4096:
                dictionary[wk] = next_code
                next_code += 1
                if next_code > (1 << code_size) and code_size < 12:
                    code_size += 1
            else:
                emit(clear_code)
                dictionary = {}
                for i in range(clear_code):
                    dictionary[(i,)] = i
                next_code = eoi_code + 1
                code_size = min_code_size + 1
            w = (k,)

    emit(dictionary[w])
    emit(eoi_code)

    if bits_in_buffer > 0:
        result.append(buffer & 0xFF)

    return bytes(result)


def generate_starfield():
    """Generate twinkling starfield animation."""
    random.seed(42)
    stars = []
    for _ in range(40):
        stars.append({
            'x': random.randint(0, WIDTH - 1),
            'y': random.randint(0, HEIGHT - 1),
            'phase': random.uniform(0, 2 * math.pi),
            'speed': random.uniform(0.3, 1.0),
            'color_base': random.choice([
                (180, 200, 255),  # Blue-white
                (255, 220, 180),  # Warm white
                (200, 200, 255),  # Cool white
                (255, 180, 180),  # Pinkish
            ])
        })

    # Build palette: dark gradient + star colors
    palette = []
    # Dark gradient (0-63)
    for i in range(64):
        r = int(8 + i * 0.3)
        g = int(8 + i * 0.2)
        b = int(20 + i * 0.8)
        palette.append((min(r, 60), min(g, 50), min(b, 120)))
    # Star brightness levels (64-255)
    for level in range(192):
        brightness = level * 255 // 191
        palette.append((brightness, brightness, brightness))

    frames = []
    for frame in range(NUM_FRAMES):
        # Start with dark gradient
        pixels = []
        for y in range(HEIGHT):
            for x in range(WIDTH):
                ratio = y / HEIGHT
                r = int(6 + ratio * 12)
                g = int(6 + ratio * 8)
                b = int(18 + ratio * 30)
                idx = quantize_color(min(r, 60), min(g, 50), min(b, 120), palette)
                pixels.append(idx)

        # Draw stars with twinkling
        for star in stars:
            brightness = 0.5 + 0.5 * math.sin(star['phase'] + frame * star['speed'] * 0.5)
            bri = int(brightness * 255)
            r = int(star['color_base'][0] * brightness)
            g = int(star['color_base'][1] * brightness)
            b = int(star['color_base'][2] * brightness)
            idx = quantize_color(min(r, 255), min(g, 255), min(b, 255), palette)
            sx, sy = star['x'], star['y']
            if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
                pixels[sy * WIDTH + sx] = idx
            # Glow effect
            for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
                nx, ny = sx + dx, sy + dy
                if 0 <= nx < WIDTH and 0 <= ny < HEIGHT:
                    glow_bri = int(brightness * 0.3)
                    gr = int(star['color_base'][0] * brightness * 0.3)
                    gg = int(star['color_base'][1] * brightness * 0.3)
                    gb = int(star['color_base'][2] * brightness * 0.3)
                    gidx = quantize_color(min(gr, 255), min(gg, 255), min(gb, 255), palette)
                    pixels[ny * WIDTH + nx] = gidx

        frames.append(bytes(pixels))

    return frames, palette


def generate_wave():
    """Generate flowing wave animation."""
    palette = []
    # Deep blue to cyan gradient (0-127)
    for i in range(128):
        r = int(5 + i * 0.2)
        g = int(10 + i * 0.8)
        b = int(40 + i * 1.2)
        palette.append((min(r, 40), min(g, 120), min(b, 200)))
    # Cyan to white (128-255)
    for i in range(128):
        r = int(10 + i * 0.8)
        g = int(120 + i * 0.5)
        b = int(200 + i * 0.2)
        palette.append((min(r, 120), min(g, 220), min(b, 255)))

    frames = []
    for frame in range(NUM_FRAMES):
        pixels = []
        phase = frame * 0.5  # Faster animation with fewer frames
        for y in range(0, HEIGHT, 2):  # Render at half height, double pixels
            for x in range(0, WIDTH, 2):
                wave1 = math.sin(x * 0.06 + y * 0.04 + phase) * 0.5 + 0.5
                wave2 = math.sin(x * 0.04 - y * 0.06 + phase * 0.7) * 0.5 + 0.5
                combined = (wave1 + wave2) / 2.0
                depth = y / HEIGHT
                combined = combined * (1 - depth * 0.5)
                idx = int(combined * 255)
                idx = max(0, min(255, idx))
                # Duplicate pixels for 2x scale
                for dy in range(2):
                    for dx in range(2):
                        nx, ny = x + dx, y + dy
                        if nx < WIDTH and ny < HEIGHT:
                            pixels.append(idx)

        frames.append(bytes(pixels))

    return frames, palette


def generate_particles():
    """Generate floating particles animation."""
    random.seed(123)
    particles = []
    for _ in range(30):
        particles.append({
            'x': random.uniform(0, WIDTH),
            'y': random.uniform(0, HEIGHT),
            'vx': random.uniform(-0.5, 0.5),
            'vy': random.uniform(-1.5, -0.3),
            'size': random.randint(1, 3),
            'color': random.choice([
                (100, 180, 255),   # Blue
                (150, 100, 255),   # Purple
                (255, 150, 200),   # Pink
                (100, 255, 200),   # Teal
            ])
        })

    palette = []
    # Dark background gradient (0-63)
    for i in range(64):
        r = int(8 + i * 0.4)
        g = int(6 + i * 0.3)
        b = int(15 + i * 0.6)
        palette.append((min(r, 35), min(g, 30), min(b, 60)))
    # Particle colors (64-255)
    for i in range(192):
        t = i / 191
        r = int(50 + t * 200)
        g = int(50 + t * 180)
        b = int(100 + t * 155)
        palette.append((min(r, 255), min(g, 255), min(b, 255)))

    frames = []
    for frame in range(NUM_FRAMES):
        pixels = []
        for y in range(HEIGHT):
            for x in range(WIDTH):
                ratio = y / HEIGHT
                r = int(6 + ratio * 10)
                g = int(5 + ratio * 8)
                b = int(12 + ratio * 25)
                idx = quantize_color(min(r, 35), min(g, 30), min(b, 60), palette)
                pixels.append(idx)

        # Draw particles
        for p in particles:
            px = int(p['x'] + p['vx'] * frame) % WIDTH
            py = int(p['y'] + p['vy'] * frame) % HEIGHT
            bri = 0.5 + 0.5 * math.sin(frame * 0.3 + p['x'])
            cr = int(p['color'][0] * bri)
            cg = int(p['color'][1] * bri)
            cb = int(p['color'][2] * bri)
            idx = quantize_color(min(cr, 255), min(cg, 255), min(cb, 255), palette)
            for dx in range(-p['size'], p['size'] + 1):
                for dy in range(-p['size'], p['size'] + 1):
                    nx, ny = px + dx, py + dy
                    if 0 <= nx < WIDTH and 0 <= ny < HEIGHT:
                        pixels[ny * WIDTH + nx] = idx

        frames.append(bytes(pixels))

    return frames, palette


def save_as_c_array(name, frames, palette, output_file):
    """Save GIF frames as C array for LVGL."""
    with open(output_file, 'w') as f:
        f.write(f'// Auto-generated idle background animation: {name}\n')
        f.write(f'// {WIDTH}x{HEIGHT}, {len(frames)} frames, 256 colors\n\n')
        f.write('#pragma once\n\n')
        f.write('#include <cstdint>\n\n')

        # Palette
        f.write(f'static const uint8_t {name}_palette[256][3] = {{\n')
        for r, g, b in palette:
            f.write(f'    {{{r}, {g}, {b}}},\n')
        f.write('};\n\n')

        # Frames
        for i, frame in enumerate(frames):
            f.write(f'static const uint8_t {name}_frame{i}[{len(frame)}] = {{\n')
            for j in range(0, len(frame), 16):
                chunk = frame[j:j + 16]
                hex_vals = ', '.join(f'0x{b:02x}' for b in chunk)
                f.write(f'    {hex_vals},\n')
            f.write('};\n\n')

        # Frame pointers array
        f.write(f'static const uint8_t* {name}_frames[] = {{\n')
        for i in range(len(frames)):
            f.write(f'    {name}_frame{i},\n')
        f.write('};\n\n')

        f.write(f'static constexpr int {name}_frame_count = {len(frames)};\n')
        f.write(f'static constexpr int {name}_width = {WIDTH};\n')
        f.write(f'static constexpr int {name}_height = {HEIGHT};\n')


def main():
    output_dir = os.path.join(os.path.dirname(__file__), '..', 'main', 'boards',
                               'bread-compact-wifi-lcd', 'idle_animations')
    os.makedirs(output_dir, exist_ok=True)

    print("Generating starfield animation...")
    frames, palette = generate_starfield()
    write_gif(os.path.join(output_dir, 'starfield.gif'), frames, palette, WIDTH, HEIGHT, 100)
    save_as_c_array('starfield', frames, palette, os.path.join(output_dir, 'starfield.h'))
    print(f"  -> starfield.gif ({os.path.getsize(os.path.join(output_dir, 'starfield.gif'))} bytes)")

    print("Generating wave animation...")
    frames, palette = generate_wave()
    write_gif(os.path.join(output_dir, 'wave.gif'), frames, palette, WIDTH, HEIGHT, 80)
    save_as_c_array('wave', frames, palette, os.path.join(output_dir, 'wave.h'))
    print(f"  -> wave.gif ({os.path.getsize(os.path.join(output_dir, 'wave.gif'))} bytes)")

    print("Generating particles animation...")
    frames, palette = generate_particles()
    write_gif(os.path.join(output_dir, 'particles.gif'), frames, palette, WIDTH, HEIGHT, 100)
    save_as_c_array('particles', frames, palette, os.path.join(output_dir, 'particles.h'))
    print(f"  -> particles.gif ({os.path.getsize(os.path.join(output_dir, 'particles.gif'))} bytes)")

    print(f"\nAll files saved to {output_dir}")


if __name__ == '__main__':
    main()
