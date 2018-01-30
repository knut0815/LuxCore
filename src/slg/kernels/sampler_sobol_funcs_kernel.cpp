#include <string>
namespace slg { namespace ocl {
std::string KernelSource_sampler_sobol_funcs = 
"#line 2 \"sampler_sobol_funcs.cl\"\n"
"\n"
"/***************************************************************************\n"
" * Copyright 1998-2018 by authors (see AUTHORS.txt)                        *\n"
" *                                                                         *\n"
" *   This file is part of LuxCoreRender.                                   *\n"
" *                                                                         *\n"
" * Licensed under the Apache License, Version 2.0 (the \"License\");         *\n"
" * you may not use this file except in compliance with the License.        *\n"
" * You may obtain a copy of the License at                                 *\n"
" *                                                                         *\n"
" *     http://www.apache.org/licenses/LICENSE-2.0                          *\n"
" *                                                                         *\n"
" * Unless required by applicable law or agreed to in writing, software     *\n"
" * distributed under the License is distributed on an \"AS IS\" BASIS,       *\n"
" * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*\n"
" * See the License for the specific language governing permissions and     *\n"
" * limitations under the License.                                          *\n"
" ***************************************************************************/\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Sobol Sequence\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if (PARAM_SAMPLER_TYPE == 2) || ((PARAM_SAMPLER_TYPE == 3) && defined(RENDER_ENGINE_TILEPATHOCL))\n"
"\n"
"uint SobolSequence_SobolDimension(const uint index, const uint dimension) {\n"
"	const uint offset = dimension * SOBOL_BITS;\n"
"	uint result = 0;\n"
"	uint i = index;\n"
"\n"
"	for (uint j = 0; i; i >>= 1, j++) {\n"
"		if (i & 1)\n"
"			result ^= SOBOL_DIRECTIONS[offset + j];\n"
"	}\n"
"\n"
"	return result;\n"
"}\n"
"\n"
"float SobolSequence_GetSample(__global Sample *sample, const uint index) {\n"
"	const uint pass = sample->pass;\n"
"\n"
"	// I scramble pass too in order avoid correlations visible with LIGHTCPU and PATHCPU\n"
"	const uint iResult = SobolSequence_SobolDimension(pass + sample->rngPass, index);\n"
"	const float fResult = iResult * (1.f / 0xffffffffu);\n"
"\n"
"	// Cranley-Patterson rotation to reduce visible regular patterns\n"
"	const float shift = (index & 1) ? sample->rng0 : sample->rng1;\n"
"	const float val = fResult + shift;\n"
"\n"
"	return val - floor(val);\n"
"}\n"
"\n"
"#endif\n"
"\n"
"//------------------------------------------------------------------------------\n"
"// Sobol Sampler Kernel\n"
"//------------------------------------------------------------------------------\n"
"\n"
"#if (PARAM_SAMPLER_TYPE == 2)\n"
"\n"
"void SamplerSharedData_GetNewBucket(__global SamplerSharedData *samplerSharedData,\n"
"		const uint filmRegionPixelCount,\n"
"		uint *pixelBucketIndex, uint *pass, uint *seed) {\n"
"	*pixelBucketIndex = atomic_inc(&samplerSharedData->pixelBucketIndex) %\n"
"				(filmRegionPixelCount / SOBOL_OCL_WORK_SIZE);\n"
"\n"
"	// The array of fields is attached to the SamplerSharedData structure\n"
"	__global uint *bucketPass = (__global uint *)(&samplerSharedData->pixelBucketIndex + sizeof(SamplerSharedData));\n"
"	*pass = atomic_inc(&bucketPass[*pixelBucketIndex]);\n"
"\n"
"	*seed = (samplerSharedData->seedBase + *pixelBucketIndex) % (0xFFFFFFFFu - 1u) + 1u;\n"
"}\n"
"\n"
"void Sampler_InitNewSample(Seed *seed,\n"
"		__global SamplerSharedData *samplerSharedData,\n"
"		__global Sample *sample, __global float *sampleDataPathBase,\n"
"		const uint filmWidth, const uint filmHeight,\n"
"		const uint filmSubRegion0, const uint filmSubRegion1,\n"
"		const uint filmSubRegion2, const uint filmSubRegion3) {\n"
"	const uint filmRegionPixelCount = (filmSubRegion1 - filmSubRegion0 + 1) * (filmSubRegion3 - filmSubRegion2 + 1);\n"
"\n"
"	// Update pixelIndexOffset\n"
"\n"
"	uint pixelIndexBase  = sample->pixelIndexBase;\n"
"	uint pixelIndexOffset = sample->pixelIndexOffset;\n"
"	uint pass = sample->pass;\n"
"	// pixelIndexRandomStart is used to jitter the order of the pixel rendering\n"
"	uint pixelIndexRandomStart = sample->pixelIndexRandomStart;\n"
"\n"
"	pixelIndexOffset++;\n"
"	if ((pixelIndexOffset >= SOBOL_OCL_WORK_SIZE) ||\n"
"			(pixelIndexBase + pixelIndexOffset >= filmRegionPixelCount)) {\n"
"		// Ask for a new base\n"
"		uint bucketSeed;\n"
"		SamplerSharedData_GetNewBucket(samplerSharedData, filmRegionPixelCount,\n"
"				&pixelIndexBase, &pass, &bucketSeed);\n"
"		\n"
"		// Transform the bucket index in a pixel index\n"
"		pixelIndexBase = pixelIndexBase * SOBOL_OCL_WORK_SIZE;\n"
"		pixelIndexOffset = 0;\n"
"\n"
"		sample->pixelIndexBase = pixelIndexBase;\n"
"		sample->pass = pass;\n"
"		\n"
"		pixelIndexRandomStart = Floor2UInt(Rnd_FloatValue(seed) * SOBOL_OCL_WORK_SIZE);\n"
"		sample->pixelIndexRandomStart = pixelIndexRandomStart;\n"
"\n"
"		Seed rngGeneratorSeed;\n"
"		Rnd_Init(bucketSeed, &rngGeneratorSeed);\n"
"		sample->rngGeneratorSeed = rngGeneratorSeed;\n"
"	}\n"
"	\n"
"	// Save the new value\n"
"	sample->pixelIndexOffset = pixelIndexOffset;\n"
"\n"
"	// Initialize rng0 and rng1\n"
"\n"
"	Seed rngGeneratorSeed = sample->rngGeneratorSeed;\n"
"	// Limit the number of pass skipped\n"
"	sample->rngPass = Rnd_UintValue(&rngGeneratorSeed) % 512;\n"
"	sample->rng0 = Rnd_FloatValue(&rngGeneratorSeed);\n"
"	sample->rng1 = Rnd_FloatValue(&rngGeneratorSeed);\n"
"	sample->rngGeneratorSeed = rngGeneratorSeed;\n"
"\n"
"	// Initialize IDX_SCREEN_X and IDX_SCREEN_Y sample\n"
"\n"
"	const uint pixelIndex = (pixelIndexBase + pixelIndexOffset + pixelIndexRandomStart) % filmRegionPixelCount;\n"
"	const uint subRegionWidth = filmSubRegion1 - filmSubRegion0 + 1;\n"
"	const uint pixelX = filmSubRegion0 + (pixelIndex % subRegionWidth);\n"
"	const uint pixelY = filmSubRegion2 + (pixelIndex / subRegionWidth);\n"
"\n"
"	sampleDataPathBase[IDX_SCREEN_X] = pixelX + SobolSequence_GetSample(sample, IDX_SCREEN_X);\n"
"	sampleDataPathBase[IDX_SCREEN_Y] = pixelY + SobolSequence_GetSample(sample, IDX_SCREEN_Y);\n"
"}\n"
"\n"
"__global float *Sampler_GetSampleData(__global Sample *sample, __global float *samplesData) {\n"
"	const size_t gid = get_global_id(0);\n"
"	return &samplesData[gid * TOTAL_U_SIZE];\n"
"}\n"
"\n"
"__global float *Sampler_GetSampleDataPathBase(__global Sample *sample, __global float *sampleData) {\n"
"	return sampleData;\n"
"}\n"
"\n"
"__global float *Sampler_GetSampleDataPathVertex(__global Sample *sample,\n"
"		__global float *sampleDataPathBase, const uint depth) {\n"
"	// This is never used in Sobol sampler\n"
"	return &sampleDataPathBase[IDX_BSDF_OFFSET + (depth - 1) * VERTEX_SAMPLE_SIZE];\n"
"}\n"
"\n"
"float Sampler_GetSamplePath(Seed *seed, __global Sample *sample,\n"
"		__global float *sampleDataPathBase, const uint index) {\n"
"	switch (index) {\n"
"		case IDX_SCREEN_X:\n"
"			return sampleDataPathBase[IDX_SCREEN_X];\n"
"		case IDX_SCREEN_Y:\n"
"			return sampleDataPathBase[IDX_SCREEN_Y];\n"
"		default:\n"
"			return SobolSequence_GetSample(sample, index);\n"
"	}\n"
"}\n"
"\n"
"float Sampler_GetSamplePathVertex(Seed *seed, __global Sample *sample,\n"
"		__global float *sampleDataPathVertexBase,\n"
"		const uint depth, const uint index) {\n"
"	// The depth used here is counted from the first hit point of the path\n"
"	// vertex (so it is effectively depth - 1)\n"
"	if (depth < SOBOL_MAX_DEPTH)\n"
"		return SobolSequence_GetSample(sample, IDX_BSDF_OFFSET + depth * VERTEX_SAMPLE_SIZE + index);\n"
"	else\n"
"		return Rnd_FloatValue(seed);\n"
"}\n"
"\n"
"void Sampler_SplatSample(\n"
"		Seed *seed,\n"
"		__global SamplerSharedData *samplerSharedData,\n"
"		__global Sample *sample, __global float *sampleData\n"
"		FILM_PARAM_DECL\n"
"		) {\n"
"	Film_AddSample(sample->result.pixelX, sample->result.pixelY,\n"
"			&sample->result, 1.f\n"
"			FILM_PARAM);\n"
"}\n"
"\n"
"void Sampler_NextSample(\n"
"		Seed *seed,\n"
"		__global SamplerSharedData *samplerSharedData,\n"
"		__global Sample *sample,\n"
"		__global float *sampleData,\n"
"		const uint filmWidth, const uint filmHeight,\n"
"		const uint filmSubRegion0, const uint filmSubRegion1,\n"
"		const uint filmSubRegion2, const uint filmSubRegion3) {\n"
"	Sampler_InitNewSample(seed, samplerSharedData, sample, sampleData, filmWidth, filmHeight,\n"
"			filmSubRegion0, filmSubRegion1, filmSubRegion2, filmSubRegion3);\n"
"}\n"
"\n"
"bool Sampler_Init(Seed *seed, __global SamplerSharedData *samplerSharedData,\n"
"		__global Sample *sample, __global float *sampleData,\n"
"		const uint filmWidth, const uint filmHeight,\n"
"		const uint filmSubRegion0, const uint filmSubRegion1,\n"
"		const uint filmSubRegion2, const uint filmSubRegion3) {\n"
"	sample->pixelIndexOffset = SOBOL_OCL_WORK_SIZE;\n"
"\n"
"	Sampler_NextSample(seed, samplerSharedData, sample, sampleData, filmWidth, filmHeight,\n"
"			filmSubRegion0, filmSubRegion1, filmSubRegion2, filmSubRegion3);\n"
"\n"
"	return true;\n"
"}\n"
"\n"
"#endif\n"
; } }
