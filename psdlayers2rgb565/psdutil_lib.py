# psdutil_lib.py
#
# 設定ファイルの読み込みや、画像変換のライブラリ
#
# Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.
# see https://opensource.org/licenses/MIT
import os
import sys
import re
import array
from PIL import Image, ImageOps
import math

## 透明色の置き換え
transparent_replacement_rgb565 = 0b0000000000100000
specific_replacement_rgb565    = 0b0000000000000000

## 設定ファイル：ファイル名から座標情報を抽出
def parse_layers_line(line):
    match = re.match(r'.+?\.W(\d+)_H(\d+)_X(\d+)_Y(\d+)\.png', line)
    if match:
        w, h, x, y = match.groups()
        return {'filename': line, 'w': int(w), 'h': int(h), 'x': int(x), 'y': int(y)}
    else:
        return None

## 設定ファイル：設定ファイルを読み込む
def load_configfile(file_path):
    conf = {}
    current_section = None
    current_subsection = None
    current_dict = None
    inside_files_block = False
    current_key = None

    with open(file_path, 'r', encoding='utf-8') as file:
        for line in file:
            line = line.strip()
            if not line or re.match(r'^[#;]', line):
                continue
            if line == "[end]":
                break

            section_match = re.match(r'^\[\[?(\w+)\]?\]$', line)
            if section_match:
                current_section = section_match.group(1)
                current_subsection = None
                inside_files_block = False
                if re.match(r'^\[\[\w+\]\]$', line):
                    current_subsection = current_section
                    if current_subsection not in conf:
                        conf[current_subsection] = []
                    current_dict = {}
                    conf[current_subsection].append(current_dict)
                else:
                    conf[current_section] = {}
                    current_dict = conf[current_section]
                continue

            if inside_files_block:
                if line == '</files>':
                    inside_files_block = False
                else:
                    layer_data = parse_layers_line(line)
                    if layer_data:
                        current_dict[current_key].append(layer_data)
                continue

            key_value_match = re.match(r'^(\w+)\s*=\s*(.+)$', line)
            if key_value_match:
                key, value = key_value_match.groups()
                if value == '<files>':
                    inside_files_block = True
                    current_key = key
                    current_dict[current_key] = []
                    continue
                value = [int(v.strip()) if v.strip().isdigit() else v.strip() for v in value.split(',')] if ',' in value else (int(value.strip()) if value.strip().isdigit() else value.strip())
                current_dict[key] = value.strip() if isinstance(value, str) else value
                continue
    return conf

## RGB565：減色処理
def rgb_to_rgb565(r, g, b):
    r_565 = (r >> 3) & 0x1F
    g_565 = (g >> 2) & 0x3F
    b_565 = (b >> 3) & 0x1F
    return (r_565 << 11) | (g_565 << 5) | b_565

## RGB565：イメージデータをRGB565バイナリに変換する
def convert_image_to_rgb565(image):
    pixels_alpha = image.load()
    #byte_array_fixed = bytearray()
    byte_array_fixed = array.array("H")
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = pixels_alpha[x, y]
            if a < 64:  #alpha値が低いピクセルのRGBが異常値になるのを防ぐ（完全ではない…）
                rgb565 = transparent_replacement_rgb565
            else:
                rgb565 = rgb_to_rgb565(r, g, b)
                if rgb565 == transparent_replacement_rgb565:
                    rgb565 = specific_replacement_rgb565
            #byte_array_fixed.append(rgb565 >> 8)
            #byte_array_fixed.append(rgb565 & 0xFF)
            byte_array_fixed.append(rgb565)
    return byte_array_fixed

## 画像を合成する
def convert_pnglayer2rgb565(conf, data, png_dir, ratio=1.0):
    posx, posy, sizew, sizeh = conf['offset'][data['parts']]
    canvas = Image.new('RGBA', (sizew, sizeh), (0, 0, 0, 0))
    #out_png_path = os.path.join(png_dir, data['name']+".png")
    out_png_path = os.path.join(png_dir, f"{data['parts']}-{data['pidx']}.png")

    ## レイヤーを1つ1つ重ね合わせる
    for layer in reversed(data['layers']):
        png_path = os.path.join(png_dir, layer['filename'])
        image = Image.open(png_path).convert("RGBA")
        pos = (layer['x'] - posx, layer['y'] - posy)
        canvas.paste(image, pos, image)

    ## 縮小する
    resize_wh = (math.ceil(canvas.width * ratio), math.ceil(canvas.height * ratio))
    canvas = canvas.resize(resize_wh, Image.LANCZOS)
    reposx = round(posx * ratio)
    reposy = round(posy * ratio)

    ## 反転する
    if conf['resize']['mirror'] == 1:
        canvas = ImageOps.mirror(canvas)
        partx = reposx - round(conf['offset']['body'][0]*ratio)
        reposx = round(conf['offset']['body'][0]*ratio) + round(conf['offset']['body'][2]*ratio) - resize_wh[0] - partx

    ## PNGファイルを保存する
    canvas.save(out_png_path)
    print(f"Saved: {out_png_path}")

    ## RGB565バイナリに変換する
    binsize = canvas.width * canvas.height
    binsize2 = binsize * 2
    rgb565bin = convert_image_to_rgb565(canvas)

    ## コメントを作成する
    comment1 = (f" * [<num>] : {data['title']}\n"
                f" *       {canvas.width} x {canvas.height} , {binsize2} bytes\n")
    for layer in data['layers']:
        comment1 += f" *       {layer['filename']}\n"
    comment2 = f" * [<num>] : {data['title']}\n"

    ## 変換終了
    return {
        'png': out_png_path,
        'rgb565': (canvas.width, canvas.height, binsize, reposx, reposy, rgb565bin, data['parts'], data['title']),
        'comment1': comment1,
        'comment2': comment2,
    }

## RGB565：RGB565バイナリからC++用のヘッダーを作成
def generate_header(images_info, prefix=""):
    # 画像の個別配列とポインタの配列の定義
    table = {}
    table2 = {}
    img_bin_arrays = ""
    img_imginfo_arrays = f"const zundavatar::ImageInfo {prefix}Info[] PROGMEM = {{\n"
    
    # 各画像データの処理
    for idx, img_info in enumerate(images_info):
        width, height, img_size, posx, posy, byte_array, parts, title = img_info
        img_bin_arrays += f"const unsigned short {prefix}Bin{idx}[{img_size}] PROGMEM = {{  \n"
        for i, byte in enumerate(byte_array):
            byte = (byte & 0xFF) << 8 | (byte & 0xFF00) >> 8  # エンディアンの変更
            if i % 8 == 0 and i != 0:
                img_bin_arrays += "\n  "
            img_bin_arrays += f"0x{byte:04X}, "
        img_bin_arrays = img_bin_arrays.rstrip(', ')
        img_bin_arrays += "};\n"
        table.setdefault(parts, []).append(idx)
        table2.setdefault(parts, []).append(title)
        comma = "," if idx < len(images_info)-1 else ""
        img_imginfo_arrays += f"  {{{prefix}Bin{idx}, {width}, {height}, {img_size}, {posx}, {posy}, 0x{transparent_replacement_rgb565:04X}}}{comma}\t\t// [{idx}] {title}\n"
    img_imginfo_arrays = img_imginfo_arrays.rstrip(',\n') + "\n};\n"

    # テーブルの処理
    table_content = "// 画像パーツの部位別テーブル\n"
    for parts, ary in table.items():
        csv = ", ".join(map(str, ary))
        table_content += f"uint16_t {prefix}Table{parts.capitalize()}[] = {{ {csv} }};\n"
        for i, title in enumerate(table2[parts]):
            table_content += f"  // [{i}] = {title}\n"

    # 最終的なヘッダー内容の結合
    header_content = img_bin_arrays + "\n" + img_imginfo_arrays
    return (header_content, table_content)

