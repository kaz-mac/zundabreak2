# psdlayers2png.py
#
# PSDファイルを読み込んで各レイヤー毎にPNGファイルに変換し、レイヤー一覧をCSV形式で出力する
# .pngファイルと.csvファイルは png-dir/ に保存される
#
# 使い方
#   python psdlayers2png.py psd-file png-dir
#
# Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
# Released under the MIT license.
# see https://opensource.org/licenses/MIT
from psd_tools import PSDImage
import os
import sys
import csv

## 保存するカタログファイルのファイル名
OUT_CSV_FILENAME = "catalog.csv"

## ファイル名に使用できない(しない)文字を置換する
def replace_invalid_characters(filename):
    invalid_characters = r'\/:*?"<>|.'
    for char in invalid_characters:
        filename = filename.replace(char, '-')
    return filename

## レイヤーの情報を再帰的に取得し、レイヤー毎のPNGファイルを保存する
def get_layer_info_and_savepng(layers, out_dir, catalog=[], path=""):
    for layer in reversed(layers):
        visible = layer.is_visible()
        layer.visible = True
        layername = layer.name.replace(',', '-')
        nextpath = layername if (path == "") else path+","+layername
        if (layer.is_group()):
            print("Group: "+nextpath)
            catalog = get_layer_info_and_savepng(layer, out_dir, catalog, nextpath)
        else:
            ## カタログ作成
            fpath = replace_invalid_characters(nextpath)
            basename = os.path.splitext(os.path.basename(in_psd_path))[0]
            filename = f"{fpath}.W{layer.width}_H{layer.height}_X{layer.left}_Y{layer.top}.png"
            catalog.append({
                'file': filename,
                'layer': nextpath,
                'width': layer.width,
                'height': layer.height, 
                'x': layer.left, 
                'y': layer.top,
                'visible': visible,
            })
            ## ファイル保存
            print("SaveFile: "+filename)
            output_path = os.path.join(out_dir, filename)
            layer_image = layer.composite()
            layer_image.save(output_path)
    return catalog

## PSDファイルをPNG形式に変換する
def save_layers_as_png(psd_file_path, out_dir):
    psd = PSDImage.open(psd_file_path)
    catalog = get_layer_info_and_savepng(psd, out_dir)
    return catalog

## ファイル一覧のCSVファイルを作成する
def create_catalog(catalog, out_dir):
    output_path = os.path.join(out_dir, OUT_CSV_FILENAME)
    with open(output_path, mode="w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file, quoting=csv.QUOTE_MINIMAL)
        writer.writerow([ "ファイル名", "幅", "高さ", "座標X", "座標Y", "レイヤー" ])
        for f in catalog:
            onoff = "ON" if f['visible'] else ""
            ary = [ f['file'], f['width'], f['height'], f['x'], f['y'] ] + f['layer'].split(",")
            writer.writerow(ary)

## メイン
if __name__ == "__main__":
    if len(sys.argv) != 3:
        exit("Usage: python psdlayers2png.py psd-file png-dir")
    in_psd_path = sys.argv[1]
    out_png_dir = sys.argv[2]
    if not os.path.exists(in_psd_path):
        exit("Error: psd-file not found.")

    ## 変換実行
    if not os.path.exists(out_png_dir):
        os.makedirs(out_png_dir)
    catalog = save_layers_as_png(in_psd_path, out_png_dir)
    create_catalog(catalog, out_png_dir)
