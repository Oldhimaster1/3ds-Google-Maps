#!/usr/bin/env python3
"""Compile the real repository QR encoder and scan its output independently."""
import ctypes
import pathlib
import subprocess
import tempfile
import numpy as np
import cv2
ROOT=pathlib.Path(__file__).resolve().parents[2]
URLS=[
    "https://192.168.1.118:8443",
    "https://10.0.0.5:8443",
    "https://172.20.10.2:8443",
    "https://192.168.100.100:8443",
    "https://192.168.1.118:8443/gps"
]
with tempfile.TemporaryDirectory(prefix="gps-qr-test-") as temp:
    binary=pathlib.Path(temp)/"qrcode.so"
    subprocess.run(["cc","-std=c11","-O2","-fPIC","-shared","-I"+str(ROOT/"include"),
        str(ROOT/"source/qrcode.c"),"-o",str(binary)],check=True)
    lib=ctypes.CDLL(str(binary))
    lib.qr_encode.argtypes=[ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_int)]
    lib.qr_encode.restype=ctypes.c_int
    for url in URLS:
        raw=url.encode()
        modules=(ctypes.c_uint8*(33*33))()
        size=ctypes.c_int()
        v=lib.qr_encode(raw,len(raw),modules,ctypes.byref(size))
        assert 1<=v<=4 and size.value==17+4*v,(url,v,size.value)
        scale,quiet=7,4
        image=np.full(((size.value+8)*scale,)*2,255,np.uint8)
        for y in range(size.value):
            for x in range(size.value):
                if modules[y*size.value+x]:
                    image[(y+quiet)*scale:(y+quiet+1)*scale,
                          (x+quiet)*scale:(x+quiet+1)*scale]=0
        decoded,points,_=cv2.QRCodeDetector().detectAndDecode(image)
        assert points is not None and decoded==url,(url,decoded,v)
        # Also reproduce the QR's real 400x240 top-screen geometry.
        # Quiet zone must stay white and modules pixel-aligned. Verify that
        # mild rotation, camera blur, and JPEG compression still decode.
        px=max(2,240//(size.value+8))
        total_px=(size.value+8)*px
        x0=(400-total_px)//2
        y0=(240-total_px)//2
        screen=np.full((240,400),255,np.uint8)
        for row in range(size.value):
            for col in range(size.value):
                if modules[row*size.value+col]:
                    x=x0+(col+4)*px
                    y=y0+(row+4)*px
                    screen[y:y+px,x:x+px]=0
        rotated=cv2.warpAffine(screen,
            cv2.getRotationMatrix2D((200,120),3,1),(400,240),borderValue=255)
        jpeg=cv2.imdecode(cv2.imencode(".jpg",screen,
            [cv2.IMWRITE_JPEG_QUALITY,65])[1],cv2.IMREAD_GRAYSCALE)
        for name,screenshot in (
            ("top-screen",screen),
            ("camera-tilt",rotated),
            ("jpeg-compressed",jpeg),
            ("mild-blur",cv2.GaussianBlur(screen,(3,3),0.4))
        ):
            decoded,points,_=cv2.QRCodeDetector().detectAndDecode(screenshot)
            assert points is not None and decoded==url,(name,url,decoded)

    print("QR scanned and decoded via OpenCV:",len(URLS),"independent test URLs")
