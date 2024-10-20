# 概要
PSD形式の画像ファイルからレイヤーを抽出して合成し、Arduino用で使用できるC++のソースコード形式(RGB565)に変換するツールです。サウンドファイル等のバイナリデータをソースコード形式に変換するツールも含まれています。

# ドキュメント
このプログラムはズンダチャンに収録したツールの一部です。詳しくはこちらのドキュメントを参考にしてください。<br>
[https://github.com/kaz-mac/zundachan/tree/main/tools](https://github.com/kaz-mac/zundachan/tree/main/tools)

# 本プロジェクト用に作成したファイル
* image_zundamon128.ini - ずんだもん 分解設定ファイル 128x128用
* convert_zundamon128.bat - 一連の変換処理を一括で行うバッチファイル

# ライブラリのインストール
```
pip install pillow
pip install psd-tools
```
Python 3.11で動作確認。3.13ではpsd-toolsがインストールできなかった。

# 使用例
PSD画像を分解
```
python psdlayers2png.py "ずんだもん立ち絵素材2.3.psd" image_zundamon128_png
```
分解した画像データを.hファイルに変換
```
python pngmerger_rgb565.py image_zundamon128.ini image_zundamon128_png zundamon128.h
```
WAVファイルを.hファイルに変換
```
python file2h.py input.wav output.h
```
