//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#pragma warning( disable : 3571 )

#ifndef __COLOR_SPACE_UTILITY_HLSLI__
#define __COLOR_SPACE_UTILITY_HLSLI__

//
// Gamma ramps and encoding transfer functions
//
// Agnostic to color space though usually tightly coupled.  For instance, sRGB is both a
// color space (defined by three basis vectors and a white point) and a gamma ramp.  Gamma
// ramps are designed to reduce perceptual error when compressing floats to small integer
// values.  More precision is needed in darker gradients as the human eye is more sensitive
// to contrast with dark values.  A logarithmic curve is often used.
//

// sRGB is both a color space and a transfer function or "gamma ramp".  (It's not a true gamma
// curve, which have been deprecated along with CRT displays.)  sRGB is what almost all 8 bpc
// textures use, and when passed to an LCD monitor they look right.  sRGB textures must be
// linearized for physically correct rendering, and the linear results need to be re-encoded
// with the sRGB ramp for display.
float3 LinearToSRGB( float3 x )
{
    // Approximately pow(x, 1.0 / 2.2)
    return select(x < 0.0031308, 12.92 * x, 1.055 * pow(x, 1.0 / 2.4) - 0.055);
}

float3 SRGBToLinear( float3 x )
{
    // Approximately pow(x, 2.2)
    return select(x < 0.04045, x / 12.92, pow((x + 0.055) / 1.055, 2.4));
}

// These functions avoid pow() to efficiently approximate sRGB with an error < 0.4%.
float3 LinearToSRGB_Fast( float3 x )
{
    return select(x < 0.0031308, 12.92 * x, 1.13005 * sqrt(x - 0.00228) - 0.13448 * x + 0.005719);
}

float3 SRGBToLinear_Fast( float3 x )
{
    return select(x < 0.04045, x / 12.92, -7.43605 * x - 31.24297 * sqrt(-0.53792 * x + 1.279924) + 35.34864);
}

float3 accurateSRGBToLinear(in float3 x ) {return SRGBToLinear(x);}
float3 accurateLinearToSRGB(in float3 x ) {return LinearToSRGB(x);}
// The OETF recommended for content shown on HDTVs.  This "gamma ramp" may increase contrast as
// appropriate for viewing in a dark environment.
float3 LinearToREC709( float3 x )
{
    return select(x < 0.0181, 4.5 * x, 1.0993 * pow(x, 0.45) - 0.0993);
}

float3 REC709ToLinear( float3 x )
{
    return select(x < 0.08145, x / 4.5, pow((x + 0.0993) / 1.0993, 1.0 / 0.45));
}

// This is the new HDR transfer function, also called "PQ" for perceptual quantizer.
float3 LinearToREC2084(float3 L)
{
    float m1 = 2610.0 / 4096.0 / 4;
    float m2 = 2523.0 / 4096.0 * 128;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32;
    float c3 = 2392.0 / 4096.0 * 32;
    float3 Lp = pow(L, m1);
    return pow((c1 + c2 * Lp) / (1 + c3 * Lp), m2);
}

float3 REC2084ToLinear(float3 N)
{
    float m1 = 2610.0 / 4096.0 / 4;
    float m2 = 2523.0 / 4096.0 * 128;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32;
    float c3 = 2392.0 / 4096.0 * 32;
    float3 Np = pow(N, 1 / m2);
    return pow(max(Np - c1, 0) / (c2 - c3 * Np), 1 / m1);
}

//
// Color space conversions
//
// These assume linear (not gamma-encoded) values.  A color space conversion is a change
// of basis (like in Linear Algebra).  Since a color space is defined by three vectors--
// the basis vectors--changing space involves a matrix-vector multiplication.  Note that
// changing the color space may result in colors that are "out of bounds" because some
// color spaces have larger gamuts than others.  When converting some colors from a wide
// gamut to small gamut, negative values may result, which are inexpressible in that new
// color space.
//
// It would be ideal to build a color pipeline which never throws away inexpressible (but
// perceivable) colors.  This means using a color space that is as wide as possible.  The
// XYZ color space is the neutral, all-encompassing color space, but it has the unfortunate
// property of having negative values (specifically in X and Z).  To correct this, a further
// transformation can be made to X and Z to make them always positive.  They can have their
// precision needs reduced by dividing by Y, allowing X and Z to be packed into two UNORM8s.
// This color space is called YUV for lack of a better name.
//

// Note:  Rec.709 and sRGB share the same color primaries and white point.  Their only difference
// is the transfer curve used.

float3 REC709toREC2020( float3 RGB709 )
{
    const float3x3 ConvMat =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(ConvMat, RGB709);
}

float3 REC2020toREC709(float3 RGB2020)
{
    const float3x3 ConvMat =
    {
        1.660496, -0.587656, -0.072840,
        -0.124547, 1.132895, -0.008348,
        -0.018154, -0.100597, 1.118751
    };
    return mul(ConvMat, RGB2020);
}

float3 REC709toDCIP3( float3 RGB709 )
{
    const float3x3 ConvMat =
    {
        0.822458, 0.177542, 0.000000,
        0.033193, 0.966807, 0.000000,
        0.017085, 0.072410, 0.910505
    };
    return mul(ConvMat, RGB709);
}

float3 DCIP3toREC709( float3 RGB709 )
{
    const float3x3 ConvMat =
    {
        1.224947, -0.224947, 0.000000,
        -0.042056, 1.042056, 0.000000,
        -0.019641, -0.078651, 1.098291
    };
    return mul(ConvMat, RGB709);
}

#endif // __COLOR_SPACE_UTILITY_HLSLI__