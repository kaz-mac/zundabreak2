#
# PNG画像を横方向に分割してRGB565形式で出力する
#
# 使い方
#
# Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.
# see https://opensource.org/licenses/MIT
import os
import sys

from psdutil_lib import * 
IMAGE_SUB_PREFIX = "bgimg"

## メイン
if __name__ == "__main__":
    ## 引数
    if len(sys.argv) < 4:
        print("Usage: psdsplit_rgb565.py png-file 100 200 300...")
        #sys.exit(1)
    #png_path = sys.argv[1]
    #widths = [int(arg) for arg in sys.argv[2:]]
    png_path = "zundaroom_morning-320x240.png"
    widths = [320]#[60,200,60]
    outhpp_path = os.path.splitext(png_path)[0] + ".h"

    ## ファイルチェック
    if not os.path.isfile(png_path):
        print("Error: png-file not found")
        sys.exit(1)

    ## 変換スタート
    image = Image.open(png_path)
    src_width, src_height = image.size
    start_x = 0
    rgb565bins = []
    for i, width in enumerate(widths):
        ## PNGファイルの読み込み、指定した幅で画像を分割
        segment = image.crop((start_x, 0, start_x + width, src_height)).convert('RGBA')
        segment.save(f'png/segment_{i+1}.png')

        ## RGB565に変換する
        binsize = segment.width * segment.height
        rgb565bin = convert_image_to_rgb565(segment)
        rgb565bins.append((segment.width, segment.height, binsize, start_x, 0, rgb565bin));
        start_x += width

    ## .hppヘッダーの作成と保存
    header_text = generate_header(rgb565bins, IMAGE_SUB_PREFIX)
    with open(outhpp_path, "w", encoding="utf-8") as file:
        file.write(header_text)
    print(f"Saved: {outhpp_path}")
