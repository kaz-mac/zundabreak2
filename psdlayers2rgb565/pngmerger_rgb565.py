# pngmerger_rgb565.py
#
# 構成ファイル(.ini)の指示に従い、複数のPNGファイルを合成してRGB565形式で出力する
#
# 使い方
#   python pngmerger_rgb565.py config-file.ini png-dir output.h 
#
# Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.
# see https://opensource.org/licenses/MIT
import os
import sys
from psdutil_lib import * 

## メイン
if __name__ == "__main__":
    before_ratio = 0

    ## 引数
    if len(sys.argv) != 4:
        exit("Usage: pngmerger_rgb565.py config-file.ini png-dir output.h")
    config_path = sys.argv[1]
    png_dir = sys.argv[2]
    outhpp_path = sys.argv[3]

    ## ファイルチェック
    if not os.path.isfile(config_path):
        exit("Error: config-file not found")
    if not os.path.isdir(png_dir):
        exit("Error: png-dir not found")
    if not outhpp_path.endswith('.h'):
        exit("Error: output-file is not .h")

    ## 設定ファイルを読み込む
    conf = load_configfile(config_path)
    if conf is None:
        exit("設定ファイルを読み込めませんでした。")
    header_prefix = conf['setting']['prefix'] if 'prefix' in conf['setting'] else "img"

    ## 指定したPNGファイルが存在するか事前にチェックする
    for data in conf['data']:
        for layer in data['layers']:
            png_path = os.path.join(png_dir, layer['filename'])
            if not os.path.isfile(png_path):
                exit(f"Error: '{png_path}' file not found")

    ## 変換の開始
    resize_ratio = conf['resize']['to'] / conf['resize']['from']
    rgb565bins = []
    comment1_text = ""
    comment2_text = ""
    pidx = {}
    for idx, data in enumerate(conf['data']):
        pidx[data['parts']] = pidx[data['parts']] + 1 if data['parts'] in pidx else 0
        data['pidx'] = pidx[data['parts']]
        resource = convert_pnglayer2rgb565(conf, data, png_dir, resize_ratio)
        rgb565bins.append(resource['rgb565'])
        comment1_text += resource['comment1'].replace("<num>",str(idx))+" *\n"
        #comment2_text += resource['comment2'].replace("<num>",str(idx))

    ## .hppヘッダーの作成と保存
    header_text, table_content = generate_header(rgb565bins, header_prefix)
    with open(outhpp_path, "w", encoding="utf-8") as file:
        file.write(f"{table_content}\n/*\n{comment1_text}*/\n{header_text}\n")
    print(f"Saved: {outhpp_path}")
