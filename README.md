# RealSenseAcquisition

Simple code to capture video from the Intel RealSense 3D sensor.  The code saves color, depth, and uv coordinates with timestamps as PNG files.  This code stores an entire capture session in RAM before writing anything to disk in order to maximize the framerate

Should be compiled in 64 bit mode to maximize the amount of frames stored in RAM during the capture