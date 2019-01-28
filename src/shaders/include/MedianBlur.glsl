/*
3x3 Median
Morgan McGuire and Kyle Whitson
http://graphics.cs.williams.edu
// http://casual-effects.com/research/McGuire2008Median/median.pix

Copyright (c) Morgan McGuire and Williams College, 2006
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define s2(a, b)				  temp = a; a = min(a, b); b = max(temp, b);
#define min3(a, b, c)			  s2(a, b); s2(a, c);
#define max3(a, b, c)			  s2(b, c); s2(a, c);
#define minmax3(a, b, c)		  max3(a, b, c); s2(a, b);                                    // 3 exchanges
#define minmax4(a, b, c, d)		  s2(a, b); s2(c, d); s2(a, c); s2(b, d);                     // 4 exchanges
#define minmax5(a, b, c, d, e)	  s2(a, b); s2(c, d); min3(a, c, e); max3(b, d, e);           // 6 exchanges
#define minmax6(a, b, c, d, e, f) s2(a, d); s2(b, e); s2(c, f); min3(a, b, c); max3(d, e, f); // 7 exchanges

float Median(sampler2D image, vec2 uv)
{
	
	float v[9] = {
		textureOffset(image, uv, ivec2(-1, -1)).r,
		textureOffset(image, uv, ivec2( 0, -1)).r,
		textureOffset(image, uv, ivec2( 1, -1)).r,
		textureOffset(image, uv, ivec2(-1,  0)).r,
		textureOffset(image, uv, ivec2( 0,  0)).r,
		textureOffset(image, uv, ivec2( 1,  0)).r,
		textureOffset(image, uv, ivec2(-1,  1)).r,
		textureOffset(image, uv, ivec2( 0,  1)).r,
		textureOffset(image, uv, ivec2( 1,  1)).r
	};
    
    float temp;
    // Starting with a subset of size 6, remove the min and max each time
    minmax6(v[0], v[1], v[2], v[3], v[4], v[5]);
    minmax5(v[1], v[2], v[3], v[4], v[6]);
    minmax4(v[2], v[3], v[4], v[7]);
    minmax3(v[3], v[4], v[8]);
    return v[4];
}
