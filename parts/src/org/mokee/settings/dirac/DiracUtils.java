/*
 * Copyright (C) 2018,2020 The mokeeOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.mokee.settings.dirac;

import android.content.Context;
import android.util.Log;

public final class DiracUtils {

    protected static DiracSound mDiracSound;
    private static final String TAG = "DiracUtils";
    private static final boolean DEBUG = true;

    public static void initialize() {
        if (mDiracSound == null)
            mDiracSound = new DiracSound(0, 0);
    }

    protected static void setMusic(boolean enable) {
        if (DEBUG) Log.d(TAG, "setMusic(" + enable + ")");
        mDiracSound.setMusic(enable ? 1 : 0);
    }

    protected static boolean isDiracEnabled(Context context) {
        return mDiracSound != null && mDiracSound.getMusic() == 1;
    }

    protected static void setLevel(String preset) {
        String[] level = preset.split("\\s*,\\s*");

        if (DEBUG) Log.d(TAG, "setLevel(" + preset + ")");
        for (int band = 0; band <= level.length - 1; band++) {
            mDiracSound.setLevel(band, Float.valueOf(level[band]));
        }
    }

    protected static void setHeadsetType(int paramInt) {
         if (DEBUG) Log.d(TAG, "setHeadsetType(" + paramInt + ")");
         mDiracSound.setHeadsetType(paramInt);
    }
}
