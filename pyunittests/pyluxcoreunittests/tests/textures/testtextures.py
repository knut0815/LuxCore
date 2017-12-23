# -*- coding: utf-8 -*-
################################################################################
# Copyright 1998-2015 by authors (see AUTHORS.txt)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

import unittest
import pyluxcore

from pyluxcoreunittests.tests.utils import *
from pyluxcoreunittests.tests.imagetest import *

################################################################################
# Constant texture test
################################################################################

def TestConstantTexture(cls, params):
	StandardSceneTest(cls, params, "simple/texture-constant.cfg", "ConstantTexture")

class ConstantTexture(ImageTest):
    pass

ConstantTexture = AddTests(ConstantTexture, TestConstantTexture, GetTestCases())

################################################################################
# ImageMap texture test
################################################################################

def TestImageMapTexture(cls, params):
	StandardSceneTest(cls, params, "simple/texture-imagemap.cfg", "ImageMapTexture")

class ImageMapTexture(ImageTest):
    pass

ImageMapTexture = AddTests(ImageMapTexture, TestImageMapTexture, GetTestCases())
