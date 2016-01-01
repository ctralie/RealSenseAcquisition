#Script to stitch together bitmap images and save them as PNGs
import matplotlib.pyplot as plt
import matplotlib.image as mpimg
import scipy.io as sio
import scipy.misc
import numpy as np
import os
import struct

def imreadf(filename):
    #Read in file, converting image byte array to little endian float
    I = scipy.misc.imread(filename)
    #Image is stored in BGRA format so convert to RGBA
    I = I[:, :, [2, 1, 0, 3]]
    shape = I.shape
    I = I.flatten()
    IA = bytearray(I.tolist())
    I = np.fromstring(IA.__str__(), dtype=np.dtype('<f4'))
    return np.reshape(I, shape[0:2])
    
def imwritef(I, filename):
    IA = I.flatten().tolist()
    IA = struct.pack("%if"%len(IA), *IA)
    IA = np.fromstring(IA, dtype=np.uint8)
    IA = IA.reshape([I.shape[0], I.shape[1], 4]) ##Tricky!!  Numpy is "low-order major" and the order I have things in is 4bytes per pixel, then columns, then rows.  These are specified in reverse order
    print "IA.shape = ", IA.shape
    #Convert from RGBA format to BGRA format like the real sense saver did
    IA = IA[:, :, [2, 1, 0, 3]]
    scipy.misc.imsave(filename, IA)

def combineImages(IApath, IBpath, outpath):
        IA = scipy.misc.imread(IApath)
        IB = scipy.misc.imread(IBpath)
        I = np.zeros((IA.shape[0], IA.shape[1], 4))
        I[:, :, 0:3] = IA
        I[:, :, 3] = IB[:, :, 2]
        scipy.misc.imsave(outpath, I)
        X = imreadf(outpath)
        X[np.isinf(X)] = 0
        imwritef(X, outpath)
    
if __name__ == '__main__':
    i = 0
    while True:
        if not os.path.exists("ADepth%i.bmp"%i):
            break
        #Save point cloud striped image
        combineImages('ADepth%i.bmp'%i, 'BDepth%i.bmp'%i, 'B-cloud%i.png'%i)
        #Extract depth image from point cloud image
        X = imreadf("B-cloud%i.png"%i)
        depth = X[:, np.arange(2, X.shape[1], 3)]
        imwritef(depth, "B-depth-float%i.png"%i)
        #Save UV striped image
        combineImages('AUV%i.bmp'%i, 'BUV%i.bmp'%i, 'B-depth-uv%i.png'%i)
        #Convert color image to bmp
        XColor = scipy.misc.imread("B-color%i.bmp"%i)
        scipy.misc.imsave("B-color%i.png"%i, XColor)
        i += 1
