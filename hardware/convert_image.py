#!/usr/bin/env python3
"""
Convert PNG image to OLED bitmap format (column-major, 8 pixels per byte)
Usage: python convert_image.py input.png output.h
"""

import sys
from PIL import Image
import numpy as np

def convert_png_to_oled_bitmap(png_path, output_path, var_name="image_data"):
    # Open image and convert to RGB to handle all pixel types
    img = Image.open(png_path).convert('RGB')
    width, height = img.size
    
    # Get pixel data
    pixels = np.array(img)
    
    # Calculate number of pages (8 pixels per page)
    num_pages = (height + 7) // 8
    
    # Convert to column-major format
    # Each byte represents 8 vertical pixels (one column, one page)
    bitmap_data = []
    
    for page in range(num_pages):
        for col in range(width):
            byte = 0
            for row in range(8):
                pixel_row = page * 8 + row
                if pixel_row < height:
                    # Check if pixel is not black (any channel > threshold)
                    # All non-black pixels become white (1) in the bitmap
                    r, g, b = pixels[pixel_row, col]
                    is_white = (r > 10) or (g > 10) or (b > 10)  # Threshold to detect non-black
                    if is_white:
                        byte |= (1 << (7 - row))  # MSB is top pixel
            bitmap_data.append(byte)
    
    # Generate C header file
    with open(output_path, 'w') as f:
        f.write(f"#ifndef {var_name.upper()}_H\n")
        f.write(f"#define {var_name.upper()}_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define {var_name.upper()}_WIDTH {width}\n")
        f.write(f"#define {var_name.upper()}_HEIGHT {height}\n\n")
        f.write(f"static const uint8_t {var_name}[] = {{\n")
        
        # Write data in rows of 8 bytes
        for i in range(0, len(bitmap_data), 8):
            row = bitmap_data[i:i+8]
            hex_values = ', '.join(f'0x{b:02X}' for b in row)
            f.write(f"    {hex_values}")
            if i + 8 < len(bitmap_data):
                f.write(",")
            f.write("\n")
        
        f.write("};\n\n")
        f.write(f"#endif // {var_name.upper()}_H\n")
    
    print(f"Converted {png_path} ({width}x{height}) to {output_path}")
    print(f"Total bytes: {len(bitmap_data)}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python convert_image.py input.png output.h [var_name]")
        sys.exit(1)
    
    png_path = sys.argv[1]
    output_path = sys.argv[2]
    var_name = sys.argv[3] if len(sys.argv) > 3 else "image_data"
    
    convert_png_to_oled_bitmap(png_path, output_path, var_name)

