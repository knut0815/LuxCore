/***************************************************************************
 * Copyright 1998-2020 by authors (see AUTHORS.txt)                        *
 *                                                                         *
 *   This file is part of LuxCoreRender.                                   *
 *                                                                         *
 * Licensed under the Apache License, Version 2.0 (the "License");         *
 * you may not use this file except in compliance with the License.        *
 * You may obtain a copy of the License at                                 *
 *                                                                         *
 *     http://www.apache.org/licenses/LICENSE-2.0                          *
 *                                                                         *
 * Unless required by applicable law or agreed to in writing, software     *
 * distributed under the License is distributed on an "AS IS" BASIS,       *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
 * See the License for the specific language governing permissions and     *
 * limitations under the License.                                          *
 ***************************************************************************/

#if !defined(LUXRAYS_DISABLE_OPENCL)

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "luxrays/core/geometry/transform.h"
#include "luxrays/utils/ocl.h"
#include "luxrays/devices/ocldevice.h"
#include "luxrays/kernels/kernels.h"

#include "luxcore/cfg.h"

#include "slg/slg.h"
#include "slg/kernels/kernels.h"
#include "slg/renderconfig.h"
#include "slg/engines/pathoclbase/pathoclbase.h"

#if defined(__APPLE__)
//OSX version detection
#include <sys/sysctl.h>
#endif

using namespace std;
using namespace luxrays;
using namespace slg;

//------------------------------------------------------------------------------
// ThreadFilm
//------------------------------------------------------------------------------

PathOCLBaseOCLRenderThread::ThreadFilm::ThreadFilm(PathOCLBaseOCLRenderThread *thread) {
	film = NULL;

	// Film buffers
	channel_ALPHA_Buff = NULL;
	channel_DEPTH_Buff = NULL;
	channel_POSITION_Buff = NULL;
	channel_GEOMETRY_NORMAL_Buff = NULL;
	channel_SHADING_NORMAL_Buff = NULL;
	channel_MATERIAL_ID_Buff = NULL;
	channel_DIRECT_DIFFUSE_Buff = NULL;
	channel_DIRECT_GLOSSY_Buff = NULL;
	channel_EMISSION_Buff = NULL;
	channel_INDIRECT_DIFFUSE_Buff = NULL;
	channel_INDIRECT_GLOSSY_Buff = NULL;
	channel_INDIRECT_SPECULAR_Buff = NULL;
	channel_MATERIAL_ID_MASK_Buff = NULL;
	channel_DIRECT_SHADOW_MASK_Buff = NULL;
	channel_INDIRECT_SHADOW_MASK_Buff = NULL;
	channel_UV_Buff = NULL;
	channel_RAYCOUNT_Buff = NULL;
	channel_BY_MATERIAL_ID_Buff = NULL;
	channel_IRRADIANCE_Buff = NULL;
	channel_OBJECT_ID_Buff = NULL;
	channel_OBJECT_ID_MASK_Buff = NULL;
	channel_BY_OBJECT_ID_Buff = NULL;
	channel_SAMPLECOUNT_Buff = NULL;
	channel_CONVERGENCE_Buff = NULL;
	channel_MATERIAL_ID_COLOR_Buff = NULL;
	channel_ALBEDO_Buff = NULL;
	channel_AVG_SHADING_NORMAL_Buff = NULL;
	channel_NOISE_Buff = NULL;
	channel_USER_IMPORTANCE_Buff = NULL;
	
	// Denoiser sample accumulator buffers
	denoiser_NbOfSamplesImage_Buff = NULL;
	denoiser_SquaredWeightSumsImage_Buff = NULL;
	denoiser_MeanImage_Buff = NULL;
	denoiser_CovarImage_Buff = NULL;
	denoiser_HistoImage_Buff = NULL;

	renderThread = thread;
}

PathOCLBaseOCLRenderThread::ThreadFilm::~ThreadFilm() {
	delete film;

	FreeAllOCLBuffers();
}

void PathOCLBaseOCLRenderThread::ThreadFilm::Init(Film *engineFlm,
		const u_int threadFilmWidth, const u_int threadFilmHeight,
		const u_int *threadFilmSubRegion) {
	engineFilm = engineFlm;

	const u_int filmPixelCount = threadFilmWidth * threadFilmHeight;

	// Delete previous allocated Film
	delete film;

	// Allocate the new Film
	film = new Film(threadFilmWidth, threadFilmHeight, threadFilmSubRegion);
	film->CopyDynamicSettings(*engineFilm);
	// Engine film may have RADIANCE_PER_SCREEN_NORMALIZED channel because of
	// hybrid back/forward path tracing
	film->RemoveChannel(Film::RADIANCE_PER_SCREEN_NORMALIZED);
	film->Init();

	//--------------------------------------------------------------------------
	// Film channel buffers
	//--------------------------------------------------------------------------

	for (u_int i = 0; i < channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.size(); ++i)
		renderThread->intersectionDevice->FreeBuffer(&channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i]);

	if (film->GetRadianceGroupCount() > 8)
		throw runtime_error("PathOCL supports only up to 8 Radiance Groups");

	channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.resize(film->GetRadianceGroupCount(), NULL);
	for (u_int i = 0; i < channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.size(); ++i) {
		renderThread->intersectionDevice->AllocBufferRW(&channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i],
				nullptr, sizeof(float[4]) * filmPixelCount, "RADIANCE_PER_PIXEL_NORMALIZEDs[" + ToString(i) + "]");
	}
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::ALPHA))
		renderThread->intersectionDevice->AllocBufferRW(&channel_ALPHA_Buff, nullptr, sizeof(float[2]) * filmPixelCount, "ALPHA");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_ALPHA_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::DEPTH))
		renderThread->intersectionDevice->AllocBufferRW(&channel_DEPTH_Buff, nullptr, sizeof(float) * filmPixelCount, "DEPTH");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_DEPTH_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::POSITION))
		renderThread->intersectionDevice->AllocBufferRW(&channel_POSITION_Buff, nullptr, sizeof(float[3]) * filmPixelCount, "POSITION");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_POSITION_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::GEOMETRY_NORMAL))
		renderThread->intersectionDevice->AllocBufferRW(&channel_GEOMETRY_NORMAL_Buff, nullptr, sizeof(float[3]) * filmPixelCount, "GEOMETRY_NORMAL");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_GEOMETRY_NORMAL_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::SHADING_NORMAL))
		renderThread->intersectionDevice->AllocBufferRW(&channel_SHADING_NORMAL_Buff, nullptr, sizeof(float[3]) * filmPixelCount, "SHADING_NORMAL");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_SHADING_NORMAL_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::MATERIAL_ID))
		renderThread->intersectionDevice->AllocBufferRW(&channel_MATERIAL_ID_Buff, nullptr, sizeof(u_int) * filmPixelCount, "MATERIAL_ID");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_MATERIAL_ID_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::DIRECT_DIFFUSE))
		renderThread->intersectionDevice->AllocBufferRW(&channel_DIRECT_DIFFUSE_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "DIRECT_DIFFUSE");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_DIRECT_DIFFUSE_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::DIRECT_GLOSSY))
		renderThread->intersectionDevice->AllocBufferRW(&channel_DIRECT_GLOSSY_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "DIRECT_GLOSSY");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_DIRECT_GLOSSY_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::EMISSION))
		renderThread->intersectionDevice->AllocBufferRW(&channel_EMISSION_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "EMISSION");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_EMISSION_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::INDIRECT_DIFFUSE))
		renderThread->intersectionDevice->AllocBufferRW(&channel_INDIRECT_DIFFUSE_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "INDIRECT_DIFFUSE");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_INDIRECT_DIFFUSE_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::INDIRECT_GLOSSY))
		renderThread->intersectionDevice->AllocBufferRW(&channel_INDIRECT_GLOSSY_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "INDIRECT_GLOSSY");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_INDIRECT_GLOSSY_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::INDIRECT_SPECULAR))
		renderThread->intersectionDevice->AllocBufferRW(&channel_INDIRECT_SPECULAR_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "INDIRECT_SPECULAR");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_INDIRECT_SPECULAR_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::MATERIAL_ID_MASK)) {
		if (film->GetChannelCount(Film::MATERIAL_ID_MASK) > 1)
			throw runtime_error("PathOCL supports only 1 MATERIAL_ID_MASK");
		else
			renderThread->intersectionDevice->AllocBufferRW(&channel_MATERIAL_ID_MASK_Buff,
					nullptr, sizeof(float[2]) * filmPixelCount, "MATERIAL_ID_MASK");
	} else
		renderThread->intersectionDevice->FreeBuffer(&channel_MATERIAL_ID_MASK_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::DIRECT_SHADOW_MASK))
		renderThread->intersectionDevice->AllocBufferRW(&channel_DIRECT_SHADOW_MASK_Buff, nullptr, sizeof(float[2]) * filmPixelCount, "DIRECT_SHADOW_MASK");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_DIRECT_SHADOW_MASK_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::INDIRECT_SHADOW_MASK))
		renderThread->intersectionDevice->AllocBufferRW(&channel_INDIRECT_SHADOW_MASK_Buff, nullptr, sizeof(float[2]) * filmPixelCount, "INDIRECT_SHADOW_MASK");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_INDIRECT_SHADOW_MASK_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::UV))
		renderThread->intersectionDevice->AllocBufferRW(&channel_UV_Buff, nullptr, sizeof(float[2]) * filmPixelCount, "UV");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_UV_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::RAYCOUNT))
		renderThread->intersectionDevice->AllocBufferRW(&channel_RAYCOUNT_Buff, nullptr, sizeof(float) * filmPixelCount, "RAYCOUNT");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_RAYCOUNT_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::BY_MATERIAL_ID)) {
		if (film->GetChannelCount(Film::BY_MATERIAL_ID) > 1)
			throw runtime_error("PathOCL supports only 1 BY_MATERIAL_ID");
		else
			renderThread->intersectionDevice->AllocBufferRW(&channel_BY_MATERIAL_ID_Buff,
					nullptr, sizeof(float[4]) * filmPixelCount, "BY_MATERIAL_ID");
	} else
		renderThread->intersectionDevice->FreeBuffer(&channel_BY_MATERIAL_ID_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::IRRADIANCE))
		renderThread->intersectionDevice->AllocBufferRW(&channel_IRRADIANCE_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "IRRADIANCE");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_IRRADIANCE_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::OBJECT_ID))
		renderThread->intersectionDevice->AllocBufferRW(&channel_OBJECT_ID_Buff, nullptr, sizeof(u_int) * filmPixelCount, "OBJECT_ID");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_OBJECT_ID_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::OBJECT_ID_MASK)) {
		if (film->GetChannelCount(Film::OBJECT_ID_MASK) > 1)
			throw runtime_error("PathOCL supports only 1 OBJECT_ID_MASK");
		else
			renderThread->intersectionDevice->AllocBufferRW(&channel_OBJECT_ID_MASK_Buff,
					nullptr, sizeof(float[2]) * filmPixelCount, "OBJECT_ID_MASK");
	} else
		renderThread->intersectionDevice->FreeBuffer(&channel_OBJECT_ID_MASK_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::BY_OBJECT_ID)) {
		if (film->GetChannelCount(Film::BY_OBJECT_ID) > 1)
			throw runtime_error("PathOCL supports only 1 BY_OBJECT_ID");
		else
			renderThread->intersectionDevice->AllocBufferRW(&channel_BY_OBJECT_ID_Buff,
					nullptr, sizeof(float[4]) * filmPixelCount, "BY_OBJECT_ID");
	} else
		renderThread->intersectionDevice->FreeBuffer(&channel_BY_OBJECT_ID_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::SAMPLECOUNT))
		renderThread->intersectionDevice->AllocBufferRW(&channel_SAMPLECOUNT_Buff, nullptr, sizeof(u_int) * filmPixelCount, "SAMPLECOUNT");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_SAMPLECOUNT_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::CONVERGENCE))
		renderThread->intersectionDevice->AllocBufferRW(&channel_CONVERGENCE_Buff, nullptr, sizeof(float) * filmPixelCount, "CONVERGENCE");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_CONVERGENCE_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::MATERIAL_ID_COLOR))
		renderThread->intersectionDevice->AllocBufferRW(&channel_MATERIAL_ID_COLOR_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "MATERIAL_ID_COLOR");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_MATERIAL_ID_COLOR_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::ALBEDO))
		renderThread->intersectionDevice->AllocBufferRW(&channel_ALBEDO_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "ALBEDO");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_ALBEDO_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::AVG_SHADING_NORMAL))
		renderThread->intersectionDevice->AllocBufferRW(&channel_AVG_SHADING_NORMAL_Buff, nullptr, sizeof(float[4]) * filmPixelCount, "AVG_SHADING_NORMAL");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_AVG_SHADING_NORMAL_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::NOISE))
		renderThread->intersectionDevice->AllocBufferRW(&channel_NOISE_Buff, nullptr, sizeof(float) * filmPixelCount, "NOISE");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_NOISE_Buff);
	//--------------------------------------------------------------------------
	if (film->HasChannel(Film::USER_IMPORTANCE))
		renderThread->intersectionDevice->AllocBufferRW(&channel_USER_IMPORTANCE_Buff, nullptr, sizeof(float) * filmPixelCount, "USER_IMPORTANCE");
	else
		renderThread->intersectionDevice->FreeBuffer(&channel_USER_IMPORTANCE_Buff);

	//--------------------------------------------------------------------------
	// Film denoiser sample accumulator buffers
	//--------------------------------------------------------------------------

	if (film->GetDenoiser().IsEnabled()) {
		renderThread->intersectionDevice->AllocBufferRW(&denoiser_NbOfSamplesImage_Buff,
				nullptr, sizeof(float) * filmPixelCount, "Denoiser samples count");
		renderThread->intersectionDevice->AllocBufferRW(&denoiser_SquaredWeightSumsImage_Buff,
				nullptr, sizeof(float) * filmPixelCount, "Denoiser squared weight");
		renderThread->intersectionDevice->AllocBufferRW(&denoiser_MeanImage_Buff,
				nullptr, sizeof(float[3]) * filmPixelCount, "Denoiser mean image");
		renderThread->intersectionDevice->AllocBufferRW(&denoiser_CovarImage_Buff,
				nullptr, sizeof(float[6]) * filmPixelCount, "Denoiser covariance");
		renderThread->intersectionDevice->AllocBufferRW(&denoiser_HistoImage_Buff,
				nullptr, film->GetDenoiser().GetHistogramBinsCount() * 3 * sizeof(float) * filmPixelCount,
				"Denoiser sample histogram");
	} else {
		renderThread->intersectionDevice->FreeBuffer(&denoiser_NbOfSamplesImage_Buff);
		renderThread->intersectionDevice->FreeBuffer(&denoiser_SquaredWeightSumsImage_Buff);
		renderThread->intersectionDevice->FreeBuffer(&denoiser_MeanImage_Buff);
		renderThread->intersectionDevice->FreeBuffer(&denoiser_CovarImage_Buff);
		renderThread->intersectionDevice->FreeBuffer(&denoiser_HistoImage_Buff);
	}
}

void PathOCLBaseOCLRenderThread::ThreadFilm::FreeAllOCLBuffers() {
	// Film buffers
	for (u_int i = 0; i < channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.size(); ++i)
		renderThread->intersectionDevice->FreeBuffer(&channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i]);
	channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.clear();
	renderThread->intersectionDevice->FreeBuffer(&channel_ALPHA_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_DEPTH_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_POSITION_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_GEOMETRY_NORMAL_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_SHADING_NORMAL_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_MATERIAL_ID_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_DIRECT_DIFFUSE_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_DIRECT_GLOSSY_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_EMISSION_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_INDIRECT_DIFFUSE_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_INDIRECT_GLOSSY_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_INDIRECT_SPECULAR_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_MATERIAL_ID_MASK_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_DIRECT_SHADOW_MASK_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_INDIRECT_SHADOW_MASK_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_UV_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_RAYCOUNT_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_BY_MATERIAL_ID_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_IRRADIANCE_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_OBJECT_ID_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_OBJECT_ID_MASK_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_BY_OBJECT_ID_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_SAMPLECOUNT_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_CONVERGENCE_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_MATERIAL_ID_COLOR_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_ALBEDO_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_AVG_SHADING_NORMAL_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_NOISE_Buff);
	renderThread->intersectionDevice->FreeBuffer(&channel_USER_IMPORTANCE_Buff);

	// Film denoiser sample accumulator buffers
	renderThread->intersectionDevice->FreeBuffer(&denoiser_NbOfSamplesImage_Buff);
	renderThread->intersectionDevice->FreeBuffer(&denoiser_SquaredWeightSumsImage_Buff);
	renderThread->intersectionDevice->FreeBuffer(&denoiser_MeanImage_Buff);
	renderThread->intersectionDevice->FreeBuffer(&denoiser_CovarImage_Buff);
	renderThread->intersectionDevice->FreeBuffer(&denoiser_HistoImage_Buff);
}

u_int PathOCLBaseOCLRenderThread::ThreadFilm::SetFilmKernelArgs(cl::Kernel &kernel,
		u_int argIndex) const {
	// Film parameters
	kernel.setArg(argIndex++, film->GetWidth());
	kernel.setArg(argIndex++, film->GetHeight());

	const u_int *filmSubRegion = film->GetSubRegion();
	kernel.setArg(argIndex++, filmSubRegion[0]);
	kernel.setArg(argIndex++, filmSubRegion[1]);
	kernel.setArg(argIndex++, filmSubRegion[2]);
	kernel.setArg(argIndex++, filmSubRegion[3]);

	for (u_int i = 0; i < FILM_MAX_RADIANCE_GROUP_COUNT; ++i) {
		if (i < channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.size())
			OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i]);
		else
			OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, nullptr);
	}

	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_ALPHA_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_DEPTH_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_POSITION_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_GEOMETRY_NORMAL_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_SHADING_NORMAL_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_MATERIAL_ID_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_DIRECT_DIFFUSE_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_DIRECT_GLOSSY_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_EMISSION_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_INDIRECT_DIFFUSE_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_INDIRECT_GLOSSY_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_INDIRECT_SPECULAR_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_MATERIAL_ID_MASK_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_DIRECT_SHADOW_MASK_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_INDIRECT_SHADOW_MASK_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_UV_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_RAYCOUNT_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_BY_MATERIAL_ID_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_IRRADIANCE_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_OBJECT_ID_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_OBJECT_ID_MASK_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_BY_OBJECT_ID_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_SAMPLECOUNT_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_CONVERGENCE_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_MATERIAL_ID_COLOR_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_ALBEDO_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_AVG_SHADING_NORMAL_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_NOISE_Buff);
	OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, channel_USER_IMPORTANCE_Buff);

	// Film denoiser sample accumulator parameters
	FilmDenoiser &denoiser = film->GetDenoiser();
	if (denoiser.IsEnabled()) {
		kernel.setArg(argIndex++, (int)denoiser.IsWarmUpDone());
		kernel.setArg(argIndex++, denoiser.GetSampleGamma());
		kernel.setArg(argIndex++, denoiser.GetSampleMaxValue());
		kernel.setArg(argIndex++, denoiser.GetSampleScale());
		kernel.setArg(argIndex++, denoiser.GetHistogramBinsCount());
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, denoiser_NbOfSamplesImage_Buff);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, denoiser_SquaredWeightSumsImage_Buff);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, denoiser_MeanImage_Buff);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, denoiser_CovarImage_Buff);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, denoiser_HistoImage_Buff);

		if (denoiser.IsWarmUpDone()) {
			const vector<RadianceChannelScale> &scales = denoiser.GetRadianceChannelScales();
			for (u_int i = 0; i < FILM_MAX_RADIANCE_GROUP_COUNT; ++i) {
				if (i < channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.size()) {
					const Spectrum s = scales[i].GetScale();
					kernel.setArg(argIndex++, s.c[0]);
					kernel.setArg(argIndex++, s.c[1]);
					kernel.setArg(argIndex++, s.c[2]);
				} else {
					kernel.setArg(argIndex++, 0.f);
					kernel.setArg(argIndex++, 0.f);
					kernel.setArg(argIndex++, 0.f);					
				}
			}
		} else {
			for (u_int i = 0; i < FILM_MAX_RADIANCE_GROUP_COUNT; ++i) {
				kernel.setArg(argIndex++, 0.f);
				kernel.setArg(argIndex++, 0.f);
				kernel.setArg(argIndex++, 0.f);
			}
		}
	} else {
		kernel.setArg(argIndex++, 0);
		kernel.setArg(argIndex++, 0.f);
		kernel.setArg(argIndex++, 0.f);
		kernel.setArg(argIndex++, 0.f);
		kernel.setArg(argIndex++, 0);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, nullptr);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, nullptr);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, nullptr);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, nullptr);
		OpenCLDeviceBuffer::tmpSetKernelkArg(kernel, argIndex++, nullptr);

		for (u_int i = 0; i < FILM_MAX_RADIANCE_GROUP_COUNT; ++i) {
			kernel.setArg(argIndex++, 0.f);
			kernel.setArg(argIndex++, 0.f);
			kernel.setArg(argIndex++, 0.f);
		}
	}

	return argIndex;
}

void PathOCLBaseOCLRenderThread::ThreadFilm::RecvFilm(OpenCLIntersectionDevice *intersectionDevice) {
	// Async. transfer of the Film buffers

	for (u_int i = 0; i < channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.size(); ++i) {
		if (channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i]) {
			intersectionDevice->EnqueueReadBuffer(
				channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i],
				CL_FALSE,
				channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i]->GetSize(),
				film->channel_RADIANCE_PER_PIXEL_NORMALIZEDs[i]->GetPixels());
		}
	}

	if (channel_ALPHA_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_ALPHA_Buff,
			CL_FALSE,
			channel_ALPHA_Buff->GetSize(),
			film->channel_ALPHA->GetPixels());
	}
	if (channel_DEPTH_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_DEPTH_Buff,
			CL_FALSE,
			channel_DEPTH_Buff->GetSize(),
			film->channel_DEPTH->GetPixels());
	}
	if (channel_POSITION_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_POSITION_Buff,
			CL_FALSE,
			channel_POSITION_Buff->GetSize(),
			film->channel_POSITION->GetPixels());
	}
	if (channel_GEOMETRY_NORMAL_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_GEOMETRY_NORMAL_Buff,
			CL_FALSE,
			channel_GEOMETRY_NORMAL_Buff->GetSize(),
			film->channel_GEOMETRY_NORMAL->GetPixels());
	}
	if (channel_SHADING_NORMAL_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_SHADING_NORMAL_Buff,
			CL_FALSE,
			channel_SHADING_NORMAL_Buff->GetSize(),
			film->channel_SHADING_NORMAL->GetPixels());
	}
	if (channel_MATERIAL_ID_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_MATERIAL_ID_Buff,
			CL_FALSE,
			channel_MATERIAL_ID_Buff->GetSize(),
			film->channel_MATERIAL_ID->GetPixels());
	}
	if (channel_DIRECT_DIFFUSE_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_DIRECT_DIFFUSE_Buff,
			CL_FALSE,
			channel_DIRECT_DIFFUSE_Buff->GetSize(),
			film->channel_DIRECT_DIFFUSE->GetPixels());
	}
	if (channel_MATERIAL_ID_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_MATERIAL_ID_Buff,
			CL_FALSE,
			channel_MATERIAL_ID_Buff->GetSize(),
			film->channel_MATERIAL_ID->GetPixels());
	}
	if (channel_DIRECT_GLOSSY_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_DIRECT_GLOSSY_Buff,
			CL_FALSE,
			channel_DIRECT_GLOSSY_Buff->GetSize(),
			film->channel_DIRECT_GLOSSY->GetPixels());
	}
	if (channel_EMISSION_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_EMISSION_Buff,
			CL_FALSE,
			channel_EMISSION_Buff->GetSize(),
			film->channel_EMISSION->GetPixels());
	}
	if (channel_INDIRECT_DIFFUSE_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_INDIRECT_DIFFUSE_Buff,
			CL_FALSE,
			channel_INDIRECT_DIFFUSE_Buff->GetSize(),
			film->channel_INDIRECT_DIFFUSE->GetPixels());
	}
	if (channel_INDIRECT_GLOSSY_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_INDIRECT_GLOSSY_Buff,
			CL_FALSE,
			channel_INDIRECT_GLOSSY_Buff->GetSize(),
			film->channel_INDIRECT_GLOSSY->GetPixels());
	}
	if (channel_INDIRECT_SPECULAR_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_INDIRECT_SPECULAR_Buff,
			CL_FALSE,
			channel_INDIRECT_SPECULAR_Buff->GetSize(),
			film->channel_INDIRECT_SPECULAR->GetPixels());
	}
	if (channel_MATERIAL_ID_MASK_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_MATERIAL_ID_MASK_Buff,
			CL_FALSE,
			channel_MATERIAL_ID_MASK_Buff->GetSize(),
			film->channel_MATERIAL_ID_MASKs[0]->GetPixels());
	}
	if (channel_DIRECT_SHADOW_MASK_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_DIRECT_SHADOW_MASK_Buff,
			CL_FALSE,
			channel_DIRECT_SHADOW_MASK_Buff->GetSize(),
			film->channel_DIRECT_SHADOW_MASK->GetPixels());
	}
	if (channel_INDIRECT_SHADOW_MASK_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_INDIRECT_SHADOW_MASK_Buff,
			CL_FALSE,
			channel_INDIRECT_SHADOW_MASK_Buff->GetSize(),
			film->channel_INDIRECT_SHADOW_MASK->GetPixels());
	}
	if (channel_UV_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_UV_Buff,
			CL_FALSE,
			channel_UV_Buff->GetSize(),
			film->channel_UV->GetPixels());
	}
	if (channel_RAYCOUNT_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_RAYCOUNT_Buff,
			CL_FALSE,
			channel_RAYCOUNT_Buff->GetSize(),
			film->channel_RAYCOUNT->GetPixels());
	}
	if (channel_BY_MATERIAL_ID_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_BY_MATERIAL_ID_Buff,
			CL_FALSE,
			channel_BY_MATERIAL_ID_Buff->GetSize(),
			film->channel_BY_MATERIAL_IDs[0]->GetPixels());
	}
	if (channel_IRRADIANCE_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_IRRADIANCE_Buff,
			CL_FALSE,
			channel_IRRADIANCE_Buff->GetSize(),
			film->channel_IRRADIANCE->GetPixels());
	}
	if (channel_OBJECT_ID_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_OBJECT_ID_Buff,
			CL_FALSE,
			channel_OBJECT_ID_Buff->GetSize(),
			film->channel_OBJECT_ID->GetPixels());
	}
	if (channel_OBJECT_ID_MASK_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_OBJECT_ID_MASK_Buff,
			CL_FALSE,
			channel_OBJECT_ID_MASK_Buff->GetSize(),
			film->channel_OBJECT_ID_MASKs[0]->GetPixels());
	}
	if (channel_BY_OBJECT_ID_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_BY_OBJECT_ID_Buff,
			CL_FALSE,
			channel_BY_OBJECT_ID_Buff->GetSize(),
			film->channel_BY_OBJECT_IDs[0]->GetPixels());
	}
	if (channel_SAMPLECOUNT_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_SAMPLECOUNT_Buff,
			CL_FALSE,
			channel_SAMPLECOUNT_Buff->GetSize(),
			film->channel_SAMPLECOUNT->GetPixels());
	}
	if (channel_CONVERGENCE_Buff) {
		// This may look wrong but CONVERGENCE channel is compute by the engine
		// film convergence test on the CPU so I write instead of read (to
		// synchronize the content).
		intersectionDevice->EnqueueWriteBuffer(
			channel_CONVERGENCE_Buff,
			CL_FALSE,
			channel_CONVERGENCE_Buff->GetSize(),
			engineFilm->channel_CONVERGENCE->GetPixels());
	}
	if (channel_MATERIAL_ID_COLOR_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_MATERIAL_ID_COLOR_Buff,
			CL_FALSE,
			channel_MATERIAL_ID_COLOR_Buff->GetSize(),
			film->channel_MATERIAL_ID_COLOR->GetPixels());
	}
	if (channel_ALBEDO_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_ALBEDO_Buff,
			CL_FALSE,
			channel_ALBEDO_Buff->GetSize(),
			film->channel_ALBEDO->GetPixels());
	}
	if (channel_AVG_SHADING_NORMAL_Buff) {
		intersectionDevice->EnqueueReadBuffer(
			channel_AVG_SHADING_NORMAL_Buff,
			CL_FALSE,
			channel_AVG_SHADING_NORMAL_Buff->GetSize(),
			film->channel_AVG_SHADING_NORMAL->GetPixels());
	}
	if (channel_NOISE_Buff) {
		// This may look wrong but NOISE channel is compute by the engine
		// film noise estimation on the CPU so I write instead of read (to
		// synchronize the content).
		intersectionDevice->EnqueueWriteBuffer(
			channel_NOISE_Buff,
			CL_FALSE,
			channel_NOISE_Buff->GetSize(),
			engineFilm->channel_NOISE->GetPixels());
	}
	if (channel_USER_IMPORTANCE_Buff) {
		// This may look wrong but USER_IMPORTANCE channel is like NOISE channel
		// so I write instead of read (to synchronize the content).
		intersectionDevice->EnqueueWriteBuffer(
			channel_USER_IMPORTANCE_Buff,
			CL_FALSE,
			channel_USER_IMPORTANCE_Buff->GetSize(),
			engineFilm->channel_USER_IMPORTANCE->GetPixels());
	}

	// Async. transfer of the Film denoiser sample accumulator buffers
	FilmDenoiser &denoiser = film->GetDenoiser();
	if (denoiser.IsEnabled() && denoiser.IsWarmUpDone()) {
		intersectionDevice->EnqueueReadBuffer(
			denoiser_NbOfSamplesImage_Buff,
			CL_FALSE,
			denoiser_NbOfSamplesImage_Buff->GetSize(),
			denoiser.GetNbOfSamplesImage());
		intersectionDevice->EnqueueReadBuffer(
			denoiser_SquaredWeightSumsImage_Buff,
			CL_FALSE,
			denoiser_SquaredWeightSumsImage_Buff->GetSize(),
			denoiser.GetSquaredWeightSumsImage());
		intersectionDevice->EnqueueReadBuffer(
			denoiser_MeanImage_Buff,
			CL_FALSE,
			denoiser_MeanImage_Buff->GetSize(),
			denoiser.GetMeanImage());
		intersectionDevice->EnqueueReadBuffer(
			denoiser_CovarImage_Buff,
			CL_FALSE,
			denoiser_CovarImage_Buff->GetSize(),
			denoiser.GetCovarImage());
		intersectionDevice->EnqueueReadBuffer(
			denoiser_HistoImage_Buff,
			CL_FALSE,
			denoiser_HistoImage_Buff->GetSize(),
			denoiser.GetHistoImage());
	}
}

void PathOCLBaseOCLRenderThread::ThreadFilm::SendFilm(OpenCLIntersectionDevice *intersectionDevice) {
	// Async. transfer of the Film buffers

	for (u_int i = 0; i < channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff.size(); ++i) {
		if (channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i]) {
			intersectionDevice->EnqueueWriteBuffer(
				channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i],
				CL_FALSE,
				channel_RADIANCE_PER_PIXEL_NORMALIZEDs_Buff[i]->GetSize(),
				film->channel_RADIANCE_PER_PIXEL_NORMALIZEDs[i]->GetPixels());
		}
	}

	if (channel_ALPHA_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_ALPHA_Buff,
			CL_FALSE,
			channel_ALPHA_Buff->GetSize(),
			film->channel_ALPHA->GetPixels());
	}
	if (channel_DEPTH_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_DEPTH_Buff,
			CL_FALSE,
			channel_DEPTH_Buff->GetSize(),
			film->channel_DEPTH->GetPixels());
	}
	if (channel_POSITION_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_POSITION_Buff,
			CL_FALSE,
			channel_POSITION_Buff->GetSize(),
			film->channel_POSITION->GetPixels());
	}
	if (channel_GEOMETRY_NORMAL_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_GEOMETRY_NORMAL_Buff,
			CL_FALSE,
			channel_GEOMETRY_NORMAL_Buff->GetSize(),
			film->channel_GEOMETRY_NORMAL->GetPixels());
	}
	if (channel_SHADING_NORMAL_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_SHADING_NORMAL_Buff,
			CL_FALSE,
			channel_SHADING_NORMAL_Buff->GetSize(),
			film->channel_SHADING_NORMAL->GetPixels());
	}
	if (channel_MATERIAL_ID_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_MATERIAL_ID_Buff,
			CL_FALSE,
			channel_MATERIAL_ID_Buff->GetSize(),
			film->channel_MATERIAL_ID->GetPixels());
	}
	if (channel_DIRECT_DIFFUSE_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_DIRECT_DIFFUSE_Buff,
			CL_FALSE,
			channel_DIRECT_DIFFUSE_Buff->GetSize(),
			film->channel_DIRECT_DIFFUSE->GetPixels());
	}
	if (channel_MATERIAL_ID_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_MATERIAL_ID_Buff,
			CL_FALSE,
			channel_MATERIAL_ID_Buff->GetSize(),
			film->channel_MATERIAL_ID->GetPixels());
	}
	if (channel_DIRECT_GLOSSY_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_DIRECT_GLOSSY_Buff,
			CL_FALSE,
			channel_DIRECT_GLOSSY_Buff->GetSize(),
			film->channel_DIRECT_GLOSSY->GetPixels());
	}
	if (channel_EMISSION_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_EMISSION_Buff,
			CL_FALSE,
			channel_EMISSION_Buff->GetSize(),
			film->channel_EMISSION->GetPixels());
	}
	if (channel_INDIRECT_DIFFUSE_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_INDIRECT_DIFFUSE_Buff,
			CL_FALSE,
			channel_INDIRECT_DIFFUSE_Buff->GetSize(),
			film->channel_INDIRECT_DIFFUSE->GetPixels());
	}
	if (channel_INDIRECT_GLOSSY_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_INDIRECT_GLOSSY_Buff,
			CL_FALSE,
			channel_INDIRECT_GLOSSY_Buff->GetSize(),
			film->channel_INDIRECT_GLOSSY->GetPixels());
	}
	if (channel_INDIRECT_SPECULAR_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_INDIRECT_SPECULAR_Buff,
			CL_FALSE,
			channel_INDIRECT_SPECULAR_Buff->GetSize(),
			film->channel_INDIRECT_SPECULAR->GetPixels());
	}
	if (channel_MATERIAL_ID_MASK_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_MATERIAL_ID_MASK_Buff,
			CL_FALSE,
			channel_MATERIAL_ID_MASK_Buff->GetSize(),
			film->channel_MATERIAL_ID_MASKs[0]->GetPixels());
	}
	if (channel_DIRECT_SHADOW_MASK_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_DIRECT_SHADOW_MASK_Buff,
			CL_FALSE,
			channel_DIRECT_SHADOW_MASK_Buff->GetSize(),
			film->channel_DIRECT_SHADOW_MASK->GetPixels());
	}
	if (channel_INDIRECT_SHADOW_MASK_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_INDIRECT_SHADOW_MASK_Buff,
			CL_FALSE,
			channel_INDIRECT_SHADOW_MASK_Buff->GetSize(),
			film->channel_INDIRECT_SHADOW_MASK->GetPixels());
	}
	if (channel_UV_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_UV_Buff,
			CL_FALSE,
			channel_UV_Buff->GetSize(),
			film->channel_UV->GetPixels());
	}
	if (channel_RAYCOUNT_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_RAYCOUNT_Buff,
			CL_FALSE,
			channel_RAYCOUNT_Buff->GetSize(),
			film->channel_RAYCOUNT->GetPixels());
	}
	if (channel_BY_MATERIAL_ID_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_BY_MATERIAL_ID_Buff,
			CL_FALSE,
			channel_BY_MATERIAL_ID_Buff->GetSize(),
			film->channel_BY_MATERIAL_IDs[0]->GetPixels());
	}
	if (channel_IRRADIANCE_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_IRRADIANCE_Buff,
			CL_FALSE,
			channel_IRRADIANCE_Buff->GetSize(),
			film->channel_IRRADIANCE->GetPixels());
	}
	if (channel_OBJECT_ID_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_OBJECT_ID_Buff,
			CL_FALSE,
			channel_OBJECT_ID_Buff->GetSize(),
			film->channel_OBJECT_ID->GetPixels());
	}
	if (channel_OBJECT_ID_MASK_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_OBJECT_ID_MASK_Buff,
			CL_FALSE,
			channel_OBJECT_ID_MASK_Buff->GetSize(),
			film->channel_OBJECT_ID_MASKs[0]->GetPixels());
	}
	if (channel_BY_OBJECT_ID_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_BY_OBJECT_ID_Buff,
			CL_FALSE,
			channel_BY_OBJECT_ID_Buff->GetSize(),
			film->channel_BY_OBJECT_IDs[0]->GetPixels());
	}
	if (channel_SAMPLECOUNT_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_SAMPLECOUNT_Buff,
			CL_FALSE,
			channel_SAMPLECOUNT_Buff->GetSize(),
			film->channel_SAMPLECOUNT->GetPixels());
	}
	if (channel_CONVERGENCE_Buff) {
		// The CONVERGENCE channel is compute by the engine
		// film convergence test on the CPU so I write the engine film, not the
		// thread film.
		intersectionDevice->EnqueueWriteBuffer(
			channel_CONVERGENCE_Buff,
			CL_FALSE,
			channel_CONVERGENCE_Buff->GetSize(),
			engineFilm->channel_CONVERGENCE->GetPixels());
	}
	if (channel_MATERIAL_ID_COLOR_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_MATERIAL_ID_COLOR_Buff,
			CL_FALSE,
			channel_MATERIAL_ID_COLOR_Buff->GetSize(),
			film->channel_MATERIAL_ID_COLOR->GetPixels());
	}
	if (channel_ALBEDO_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_ALBEDO_Buff,
			CL_FALSE,
			channel_ALBEDO_Buff->GetSize(),
			film->channel_ALBEDO->GetPixels());
	}
	if (channel_AVG_SHADING_NORMAL_Buff) {
		intersectionDevice->EnqueueWriteBuffer(
			channel_AVG_SHADING_NORMAL_Buff,
			CL_FALSE,
			channel_AVG_SHADING_NORMAL_Buff->GetSize(),
			film->channel_AVG_SHADING_NORMAL->GetPixels());
	}
	if (channel_NOISE_Buff) {
		// The NOISE channel is compute by the engine
		// film noise estimation on the CPU so I write the engine film, not the
		// thread film.
		intersectionDevice->EnqueueWriteBuffer(
			channel_NOISE_Buff,
			CL_FALSE,
			channel_NOISE_Buff->GetSize(),
			engineFilm->channel_NOISE->GetPixels());
	}
	if (channel_USER_IMPORTANCE_Buff) {
		// The USER_IMPORTANCE channel is like NOISE channel
		// so I write the engine film, not the thread film.
		intersectionDevice->EnqueueWriteBuffer(
			channel_USER_IMPORTANCE_Buff,
			CL_FALSE,
			channel_USER_IMPORTANCE_Buff->GetSize(),
			engineFilm->channel_USER_IMPORTANCE->GetPixels());
	}

	// Async. transfer of the Film denoiser sample accumulator buffers
	FilmDenoiser &denoiser = film->GetDenoiser();
	if (denoiser.IsEnabled()) {
		if (denoiser.GetNbOfSamplesImage())
			intersectionDevice->EnqueueWriteBuffer(
				denoiser_NbOfSamplesImage_Buff,
				CL_FALSE,
				denoiser_NbOfSamplesImage_Buff->GetSize(),
				denoiser.GetNbOfSamplesImage());
		if (denoiser.GetSquaredWeightSumsImage())
			intersectionDevice->EnqueueWriteBuffer(
				denoiser_SquaredWeightSumsImage_Buff,
				CL_FALSE,
				denoiser_SquaredWeightSumsImage_Buff->GetSize(),
				denoiser.GetSquaredWeightSumsImage());
		if (denoiser.GetMeanImage())
			intersectionDevice->EnqueueWriteBuffer(
				denoiser_MeanImage_Buff,
				CL_FALSE,
				denoiser_MeanImage_Buff->GetSize(),
				denoiser.GetMeanImage());
		if (denoiser.GetCovarImage())
			intersectionDevice->EnqueueWriteBuffer(
				denoiser_CovarImage_Buff,
				CL_FALSE,
				denoiser_CovarImage_Buff->GetSize(),
				denoiser.GetCovarImage());
		if (denoiser.GetHistoImage())
			intersectionDevice->EnqueueWriteBuffer(
				denoiser_HistoImage_Buff,
				CL_FALSE,
				denoiser_HistoImage_Buff->GetSize(),
				denoiser.GetHistoImage());
	}
}

void PathOCLBaseOCLRenderThread::ThreadFilm::ClearFilm(cl::CommandQueue &oclQueue,
		cl::Kernel &filmClearKernel, const size_t filmClearWorkGroupSize) {
	// Set kernel arguments
	
	// This is the dummy variable required by KERNEL_ARGS_FILM macro
	filmClearKernel.setArg(0, 0);

	SetFilmKernelArgs(filmClearKernel, 1);
	
	// Clear the film
	const u_int filmPixelCount = film->GetWidth() * film->GetHeight();
	oclQueue.enqueueNDRangeKernel(filmClearKernel, cl::NullRange,
			cl::NDRange(RoundUp<u_int>(filmPixelCount, filmClearWorkGroupSize)),
			cl::NDRange(filmClearWorkGroupSize));
}

#endif
