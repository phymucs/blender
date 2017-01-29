/*
 * Copyright 2011-2016 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

CCL_NAMESPACE_BEGIN

#define ccl_get_feature_sse(pass) _mm_loadu_ps(buffer + (pass)*pass_stride)

/* Loop over the pixels in the range [low.x, high.x) x [low.y, high.y), 4 at a time.
 * pixel_buffer always points to the first of the 4 current pixel in the first pass.
 * x4 and y4 contain the coordinates of the four pixels, active_pixels contains a mask that's set for all pixels within the window. */

#ifdef DENOISE_TEMPORAL
#define FOR_PIXEL_WINDOW_SSE pixel_buffer = buffer + (low.y - rect.y)*buffer_w + (low.x - rect.x); \
                             for(int t = 0; t < num_frames; t++) { \
                                 __m128 t4 = _mm_set1_ps((t == 0)? 0: ((t <= prev_frames)? (t-prev_frames-1): (t - prev_frames))); \
                                 for(pixel.y = low.y; pixel.y < high.y; pixel.y++) { \
                                     __m128 y4 = _mm_set1_ps(pixel.y); \
                                     for(pixel.x = low.x; pixel.x < high.x; pixel.x += 4, pixel_buffer += 4) { \
                                         __m128 x4 = _mm_add_ps(_mm_set1_ps(pixel.x), _mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f)); \
                                         __m128 active_pixels = _mm_cmplt_ps(x4, _mm_set1_ps(high.x));

#define END_FOR_PIXEL_WINDOW_SSE     } \
                                     pixel_buffer += buffer_w - (pixel.x - low.x); \
                                 } \
                                 pixel_buffer += buffer_w * (buffer_h - (high.y - low.y)); \
                             }
#else
#define FOR_PIXEL_WINDOW_SSE     pixel_buffer = buffer + (low.y - rect.y)*buffer_w + (low.x - rect.x); \
                                 for(pixel.y = low.y; pixel.y < high.y; pixel.y++) { \
                                     __m128 y4 = _mm_set1_ps(pixel.y); \
                                     for(pixel.x = low.x; pixel.x < high.x; pixel.x += 4, pixel_buffer += 4) { \
                                         __m128 x4 = _mm_add_ps(_mm_set1_ps(pixel.x), _mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f)); \
                                         __m128 active_pixels = _mm_cmplt_ps(x4, _mm_set1_ps(high.x));

#define END_FOR_PIXEL_WINDOW_SSE     } \
                                     pixel_buffer += buffer_w - (pixel.x - low.x); \
                                 }
#endif

ccl_device_inline void filter_get_features_sse(__m128 x, __m128 y, __m128 t, __m128 active_pixels, float ccl_readonly_ptr buffer, __m128 *features, __m128 ccl_readonly_ptr mean, int pass_stride)
{
	__m128 *feature = features;
	*(feature++) = x;
	*(feature++) = y;
#ifdef DENOISE_TEMPORAL
	*(feature++) = t;
#endif
	*(feature++) = ccl_get_feature_sse(0);
	*(feature++) = ccl_get_feature_sse(1);
	*(feature++) = ccl_get_feature_sse(2);
	*(feature++) = ccl_get_feature_sse(3);
	*(feature++) = ccl_get_feature_sse(4);
	*(feature++) = ccl_get_feature_sse(5);
	*(feature++) = ccl_get_feature_sse(6);
	*(feature++) = ccl_get_feature_sse(7);
	if(mean) {
		for(int i = 0; i < DENOISE_FEATURES; i++)
			features[i] = _mm_mask_ps(_mm_sub_ps(features[i], mean[i]), active_pixels);
	}
	else {
		for(int i = 0; i < DENOISE_FEATURES; i++)
			features[i] = _mm_mask_ps(features[i], active_pixels);
	}
#ifdef DENOISE_SECOND_ORDER_SCREEN
	features[10] = _mm_mul_ps(features[0], features[0]);
	features[11] = _mm_mul_ps(features[1], features[1]);
	features[12] = _mm_mul_ps(features[0], features[1]);
#endif
}

ccl_device_inline void filter_get_feature_scales_sse(__m128 x, __m128 y, __m128 t, __m128 active_pixels, float ccl_readonly_ptr buffer, __m128 *scales, __m128 ccl_readonly_ptr mean, int pass_stride)
{
	*(scales++) = _mm_mask_ps(_mm_fabs_ps(_mm_sub_ps(x, *(mean++))), active_pixels); //X
	*(scales++) = _mm_mask_ps(_mm_fabs_ps(_mm_sub_ps(y, *(mean++))), active_pixels); //Y
#ifdef DENOISE_TEMPORAL
	*(scales++) = _mm_mask_ps(_mm_fabs_ps(_mm_sub_ps(t, *(mean++))), active_pixels); //T
#endif

	*(scales++) = _mm_mask_ps(_mm_fabs_ps(_mm_sub_ps(ccl_get_feature_sse(0), *(mean++))), active_pixels); //Depth

	__m128 diff = _mm_sub_ps(ccl_get_feature_sse(1), mean[0]);
	__m128 scale3 = _mm_mul_ps(diff, diff);
	diff = _mm_sub_ps(ccl_get_feature_sse(2), mean[1]);
	scale3 = _mm_add_ps(scale3, _mm_mul_ps(diff, diff));
	diff = _mm_sub_ps(ccl_get_feature_sse(3), mean[2]);
	scale3 = _mm_add_ps(scale3, _mm_mul_ps(diff, diff));
	mean += 3;
	*(scales++) = _mm_mask_ps(scale3, active_pixels); //NormalX
	*(scales++) = _mm_mask_ps(scale3, active_pixels); //NormalY
	*(scales++) = _mm_mask_ps(scale3, active_pixels); //NormalZ

	*(scales++) = _mm_mask_ps(_mm_fabs_ps(_mm_sub_ps(ccl_get_feature_sse(4), *(mean++))), active_pixels); //Shadow

	diff = _mm_sub_ps(ccl_get_feature_sse(5), mean[0]);
	scale3 = _mm_mul_ps(diff, diff);
	diff = _mm_sub_ps(ccl_get_feature_sse(6), mean[1]);
	scale3 = _mm_add_ps(scale3, _mm_mul_ps(diff, diff));
	diff = _mm_sub_ps(ccl_get_feature_sse(7), mean[2]);
	scale3 = _mm_add_ps(scale3, _mm_mul_ps(diff, diff));
	mean += 3;
	*(scales++) = _mm_mask_ps(scale3, active_pixels); //AlbedoR
	*(scales++) = _mm_mask_ps(scale3, active_pixels); //AlbedoG
	*(scales++) = _mm_mask_ps(scale3, active_pixels); //AlbedoB
}

ccl_device_inline void filter_calculate_scale_sse(__m128 *scale)
{
	scale[0] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(scale[0]), _mm_set1_ps(0.01f))); //X
	scale[1] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(scale[1]), _mm_set1_ps(0.01f))); //Y
	scale += 2;
#ifdef DENOISE_TEMPORAL
	scale[0] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(scale[0]), _mm_set1_ps(0.01f))); //T
	scale++;
#endif
	
	scale[0] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(scale[0]), _mm_set1_ps(0.01f))); //Depth

	scale[1] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(_mm_sqrt_ps(scale[1])), _mm_set1_ps(0.01f))); //NormalX
	scale[2] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(_mm_sqrt_ps(scale[2])), _mm_set1_ps(0.01f))); //NormalY
	scale[3] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(_mm_sqrt_ps(scale[3])), _mm_set1_ps(0.01f))); //NormalZ
	
	scale[4] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(scale[4]), _mm_set1_ps(0.01f))); //Shadow

	scale[5] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(_mm_sqrt_ps(scale[5])), _mm_set1_ps(0.01f))); //AlbedoR
	scale[6] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(_mm_sqrt_ps(scale[6])), _mm_set1_ps(0.01f))); //AlbedoG
	scale[7] = _mm_rcp_ps(_mm_max_ps(_mm_hmax_ps(_mm_sqrt_ps(scale[7])), _mm_set1_ps(0.01f))); //AlbedoB
}


CCL_NAMESPACE_END