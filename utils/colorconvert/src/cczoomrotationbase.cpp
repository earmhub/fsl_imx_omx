/* ------------------------------------------------------------------
 * Copyright (C) 1998-2009 PacketVideo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */

/* -------------------------------------------------------------------
 *  Copyright (c) 2011-2012, Freescale Semiconductor Inc.,
 * -------------------------------------------------------------------
 */

#include "string.h"
#include "Mem.h"

#include "colorconv_config.h"
#include "cczoomrotationbase.h"
#include "Log.h"

/**************************************************************
ASSUMPTIONS:
1. zoom ratio must be in range (1, 3]
2. cannot do zoom in in one dimenstion and zoom out in another
3. caller should take care of the aspect ratio control
4. if there are margins, the caller should paint the background to the desired color
5. all the starting address MUST be multiple of 4, which is normally the case.

**************************************************************/


#define OSCL_ARRAY_DELETE(ptr) FSL_FREE (ptr)
#define OSCL_ARRAY_NEW(T, count) FSL_MALLOC (sizeof(T) * count)

ColorConvertBase::ColorConvertBase(): _mRowPix(NULL), _mColPix(NULL), _mInitialized(false), _mState(0), _mYuvRange(false)
{
}


ColorConvertBase::~ColorConvertBase()
{
    if (_mRowPix)
    {
        OSCL_ARRAY_DELETE(_mRowPix);
    }
    if (_mColPix)
    {
        OSCL_ARRAY_DELETE(_mColPix);
    }
}


int16 ColorConvertBase::GetCapability(void)
{
    int16 cap = 0;
#if CCROTATE
    cap |= CCSUPPORT_ROTATION;
#endif
#if CCSCALING
    cap |= CCSUPPORT_SCALING;
#endif
    return cap;
}


/**************************************************************
nRotation : Rotation direction is defined at run time
            0 -- No rotation
            1 -- 90 degree counter clockwise (default)
            2 -- 180 degree rotation (upside-down)
            3 -- 90 degree clockwise
            -1 -- flip left-right
            -2 -- flip and rotate 90 degree cnt clk
            -3 -- flip and rotate 180 degree
            -4 -- flip and rotate 270 degree
**************************************************************/
int32 ColorConvertBase::Init(int32 Src_width, int32 Src_height, int32 Src_pitch,  RECTTYPE *Src_crop, OMX_COLOR_FORMATTYPE srcColorFormat, int32 Dst_width, int32 Dst_height, int32 Dst_pitch, int32 nRotation)
{

    /* check the followings, must be multiple of 2 */
    if ((Src_pitch&1) || (Dst_pitch&1) || (Dst_height&1) || (Src_height&1) || (Src_width&1))
    {
        LOG_DEBUG("cc base init fail : length is not even number");
        return 0;
    }

    _mInitialized = false;
    _mSrc_mheight = Src_height;
    _mRotation  =   nRotation;
    _mDisp.src_pitch = _mSrc_pitch  =   Src_pitch;
    _mDisp.dst_pitch = _mDst_pitch  =   Dst_pitch;
    _mDisp.src_width = _mSrc_width  =   Src_width;
    _mDisp.src_height = _mSrc_height =  Src_height;
    _mDisp.src_crop_left = Src_crop->nLeft;
    _mDisp.src_crop_top = Src_crop->nTop;
    _mDisp.src_crop_width = Src_crop->nWidth;
    _mDisp.src_crop_height = Src_crop->nHeight;
    _mDisp.dst_width = _mDst_width  =   Dst_width;
    _mDisp.dst_height = _mDst_height =  Dst_height;
    _mColorFormat = srcColorFormat;

    if(_mColorFormat != OMX_COLOR_FormatYUV420Planar &&
        _mColorFormat != OMX_COLOR_FormatYUV422Planar &&
        _mColorFormat != OMX_COLOR_FormatYUV422SemiPlanar &&
        _mColorFormat != OMX_COLOR_FormatYUV420SemiPlanar)
    {
        LOG_ERROR("cc base init fail: color %d unsupport", _mColorFormat);
        return 0;
    }

    /* Check support for rotation */
    if (_mRotation&0x1)
    {
        if (!(GetCapability()&CCSUPPORT_ROTATION))
        {
            LOG_ERROR("cc base init fail: capability not support rotation");
            return 0;
        }
    }

    _mIsFlip = false;
    if (_mRotation & 0x4)
    {
        _mIsFlip = true;
        _mRotation -= 4;
    }

    //For now, we only support the zoom ratio <=3
    if (_mRotation&0x1)
    {
        if (((Src_width*3) < Dst_height) || ((Src_height*3) < Dst_width))
        {
            LOG_ERROR("cc base init fail : length mismatch for rotation");
            return 0;
        }
    }
    else
    {
        if (((Src_width*3) < Dst_width) || ((Src_height*3) < Dst_height))
        {
            LOG_ERROR("cc base init fail : length mismatch");
            return 0;
        }
    }

    if (_mRowPix)
    {
        OSCL_ARRAY_DELETE(_mRowPix);
    }
    if (_mColPix)
    {
        OSCL_ARRAY_DELETE(_mColPix);
    }
    _mRowPix = NULL;
    _mColPix = NULL;

    if ((_mRotation&0x1) == 0)
    { /* no rotation */
        if ((_mDst_width != (uint32)_mDisp.src_crop_width) || (_mDst_height != (uint32)_mDisp.src_crop_height))
        { /* scaling */
            //calulate the Row
            printf("do scaling...");
            _mRowPix = (uint8 *) OSCL_ARRAY_NEW(uint8, _mDisp.src_crop_width);
            if (_mRowPix == NULL){
                LOG_ERROR("cc base init fail : mRowPix null");
                return 0;
            }

            _mColPix = (uint8 *) OSCL_ARRAY_NEW(uint8, _mDisp.src_crop_height);

            if (_mColPix == NULL){
                if(_mRowPix != NULL){
                    OSCL_ARRAY_DELETE(_mRowPix);
                    _mRowPix = NULL;
                }
                LOG_ERROR("cc base init fail : _mColPix null");
                return 0;
            }

            StretchLine(_mRowPix, _mDisp.src_crop_width, _mDst_width);
            StretchLine(_mColPix, _mDisp.src_crop_height, _mDst_height);
            _mIsZoom = true;
        }
        else
        {
            _mIsZoom = false;
        }
    }
    else
    { /* rotation,  */
        if ((_mDst_height != _mSrc_width) || (_mDst_width != _mSrc_height))
        { /* scaling */
            //calulate the Row
            _mRowPix = (uint8 *) OSCL_ARRAY_NEW(uint8, _mSrc_height);
            if(_mRowPix == NULL){
                LOG_ERROR("cc base init fail : _mRowPix null");
                return 0;
            }

            _mColPix = (uint8 *) OSCL_ARRAY_NEW(uint8, _mSrc_width);
            if(_mColPix == NULL){
                OSCL_ARRAY_DELETE(_mRowPix);
                _mRowPix = NULL;
                LOG_ERROR("cc base init fail : _mColPix null");
                return 0;
            }

            StretchLine(_mColPix, _mSrc_width, _mDst_height);
            StretchLine(_mRowPix, _mSrc_height, _mDst_width);
            _mIsZoom = true;
        }
        else
        {
            _mIsZoom = false;
        }
    }

    // Check support to scaling
    if (_mIsZoom)
    {
        if (!(GetCapability()&CCSUPPORT_SCALING))
        {
            LOG_ERROR("cc base init fail : capability not support scaling");
            return 0;
        }
    }

    _mInitialized = true;

    return 1;
}

void ColorConvertBase::StretchLine(uint8 *pLinePix, int32 iSrcLen, int32 iDstLen)
{
    // zoom ratio must be in range (1, 3]
    int32 e, i, j = 0, d, i2SrcLen, i2DstLen;
    bool bIsShrink = false;

    if (iSrcLen > iDstLen)
    {
        bIsShrink = true;
    }

    if (3*iSrcLen == iDstLen || 3*iDstLen == iSrcLen)
    {
        j = 3;
    }
    if (2*iSrcLen == iDstLen || 2*iDstLen == iSrcLen)
    {
        j = 2;
    }

    if (j)
    {
        if (bIsShrink)
        {
            memset(pLinePix, 0, iSrcLen);
            for (i = 0; i < iSrcLen; i += j)
            {
                pLinePix[i] = 1;
            }
        }
        else
        {
            memset(pLinePix, j, iSrcLen);
        }
        return;
    }

    //int32 xd1=0 int32 xd2=iDstLen, int32 xs1=0, int32 xs2=iSrcLen
    //int32 dx, dy2, e, d, dx2;
    //dx = xd2 - xd1;
    i2SrcLen = (iSrcLen - 1) << 1;//dy2 = (xs2 - xs1) << 1;
    e = i2SrcLen - (iDstLen - 1);//e = dy2 - dx;
    i2DstLen = (iDstLen - 1) << 1;//dx2 = dx << 1;
    i = 0;
    j = 0;
    for (d = 0; d < iDstLen /*dx*/; d++)
    {
        if (i <= iSrcLen - 1)
        {
            *(pLinePix + i) =  j;
        }
        if (e >= 0)
        {
            i++;
            e -= i2DstLen;
        }
        while (e >= 0)
        {
            if (i <= iSrcLen - 1)
            {
                *(pLinePix + i) =  j; /* zoom in case */
            }
            i++;
            e -= i2DstLen;
        }
        j++;
        e += i2SrcLen;
    }

    e = pLinePix[0];
    for (i = 1; i < iSrcLen; i++)
    {
        d = pLinePix[i];
        pLinePix[i] = (d - e); /* take difference between adjacent iRowPix[]. */
        e = d;
    }

    // add some check here to make sure that sum_{i=0...iSrcLen-1} pLinePix[i] = iDstLen
    e = 0;
    for (i = 0; i < iSrcLen; i++)
    {
        e += pLinePix[i];
    }

    if (e != iDstLen)
    {
        j = 0;
        while (e > iDstLen) /* sum greater than the destination */
        {
            while (pLinePix[j] == 0 && j < iSrcLen) /* find nonzero repetition pixel */
            {
                j++;
            }

            if (j < iSrcLen)
            {
                pLinePix[j]--;      /* remove pixel by one */
                e--;
                j++;                /* move to the next pixel */
            }
            else
            {
                j = 0;
            }
        }

        j = 0;
        while (e < iDstLen) /* sum less than the destination */
        {
            while (pLinePix[j] >= 3 && j < iSrcLen) /* find valid pixel */
            {
                j++;
            }

            if (j < iSrcLen)
            {
                pLinePix[j]++;  /* add pixel by one */
                e++;
                j++;            /* move to the next pixel */
            }
            else
            {
                j = 0;
            }
        }
    }
}
