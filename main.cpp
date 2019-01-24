/***************************************************************************************************************
 * A tool which extracts/converts the music files from the MUSIC.BIN archive used in Driver 1 (PSX) & Driver 2 *
 * Copyright (C) 2019 TecFox                                                                                   *
 ***************************************************************************************************************/

/***************************************************************************
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 2 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, write to the Free Software Foundation, Inc., *
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.             *
 *                                                                         *
 ***************************************************************************/

/*****************************************************************************************************************************************
 *
 * Developer notes
 * ---------------
 *
 * Another tool with messy code. Though this is mostly because it was never really intended to be released in any form.
 * Like with the other tool the code was simply ported from its VB6 prototype.
 *
 * Please note: Don't run this program with any file other than MUSIC.BIN. It WILL crash.
 * Since this file has no header it's impossible to identify it so the program will blindly assume that you're giving it the correct one.
 *
 *****************************************************************************************************************************************/

#include <iostream>
#include <iomanip>
#include <fstream>
#include <windows.h>
#include <math.h>

using namespace std;

// PSX ADPCM coefficients
const double K0[5] = {0, 0.9375, 1.796875, 1.53125, 1.90625};
const double K1[5] = {0, 0, -0.8125, -0.859375, -0.9375};

void* emalloc(size_t size)
{
    void* v = malloc(size);
    if(!v){
        fprintf(stderr, "An error occurred while allocating memory.\n");
        exit(EXIT_FAILURE);
    }
    return v;
}

double uniformRound(double value) {
    return floor(value + 0.5);
}

// PSX ADPCM decoding routine - decodes a single sample
short vagToPcm(unsigned char soundParameter, int soundData, double* vagPrev1, double* vagPrev2)
{
    int resultInt = 0;
    double dTmp1 = 0.0;
    double dTmp2 = 0.0;
    double dTmp3 = 0.0;

    if (soundData > 7) soundData -= 16;

    dTmp1 = (double)soundData * pow(2, (double)(12 - (soundParameter & 0x0F)));

    dTmp2 = (*vagPrev1) * K0[(soundParameter >> 4) & 0x0F];
    dTmp3 = (*vagPrev2) * K1[(soundParameter >> 4) & 0x0F];

    (*vagPrev2) = (*vagPrev1);
    (*vagPrev1) = dTmp1 + dTmp2 + dTmp3;

    resultInt = (int)uniformRound((*vagPrev1));

    if (resultInt > 32767) resultInt = 32767;
    if (resultInt < -32768) resultInt = -32768;

    return (short)resultInt;
}

// Main decoding routine - Takes PSX ADPCM formatted audio data and converts it to PCM. It also extracts the looping information if used.
void decodeSample(unsigned char* iData, short** oData, int soundSize, int* loopStart, int* loopLength)
{
    unsigned char sp = 0;
    int sd = 0;
    double vagPrev1 = 0.0;
    double vagPrev2 = 0.0;
    int lNew = 0;
    int lPrev = 0;
    int lTmp = 0;
    int k = 0;

    for (int i=0; i<soundSize; i++) {
        if (i % 16 == 0) {
            sp = iData[i];
            if ((iData[i + 1] & 0x0E) == 6) (*loopStart) = k;
            if ((iData[i + 1] & 0x0F) == 3 || (iData[i + 1] & 0x0F) == 7) (*loopLength) = (k + 28) - (*loopStart);
            i += 2;
        }
        sd = (int)iData[i] & 0x0F;
        lNew = (int)vagToPcm(sp, sd, &vagPrev1, &vagPrev2);
        lTmp = lNew - lPrev;
        if (lTmp > 32767) lTmp -= 65536;
        else if (lTmp < -32768) lTmp += 65536;
        lPrev = lNew;
        (*oData)[k++] = (short)lTmp;
        sd = ((int)iData[i] >> 4) & 0x0F;
        lNew = (int)vagToPcm(sp, sd, &vagPrev1, &vagPrev2);
        lTmp = lNew - lPrev;
        if (lTmp > 32767) lTmp -= 65536;
        else if (lTmp < -32768) lTmp += 65536;
        lPrev = lNew;
        (*oData)[k++] = (short)lTmp;
        sd = ((int)iData[i] >> 4) & 0x0F;
    }
}

// This is needed in order to deal with some corrupted pattern data in some of Driver 1's modules. It also calculates the uncompressed size.
int validatePattern(unsigned char* data, int length, int channelCount, int rowCount)
{
    int patternSize = 0;
    int curChannel = 0;
    int curRow = 0;
    int vCount = 0;

    for (int i=0; i<length; i++) {
        if (data[i] == 0xff) {
           curRow++;
           if (curRow == rowCount) {
              if (i < (length - 1)) return -1;
           }

           patternSize += (channelCount - curChannel);
           curChannel = 0;
           continue;
        }

        if ((data[i] > (channelCount - 1)) || (data[i] < curChannel)) return -1;

        if ((data[i] - curChannel) > 0) {
           patternSize += (data[i] - curChannel);
           curChannel = data[i];
        }

        i++;
        if (i > (length - 1)) return -1;

        vCount = 1;
        if (data[i] > 127) {
           if (data[i] > 159) return -1;
           vCount += (data[i] & 1) + ((data[i] >> 1) & 1) + ((data[i] >> 2) & 1) + ((data[i] >> 3) & 1) + ((data[i] >> 4) & 1);
        }
        else vCount += 4;

        patternSize += vCount;
        i += vCount - 1;
        curChannel++;

        if (i > (length - 1)) return -1;
    }

    return patternSize;
}

// Get the path without the file extension
void getFilepath(char* in, char** out)
{
    if (strlen(in) == 0) {  // in is empty - create empty string and return
        *out = (char*)emalloc(1);
        (*out)[0] = '\0';
        return;
    }

    int size;

    // Standardize directory separators
    char* pos = strchr(in, '/');
    while (pos) {
        *pos = '\\';
        pos = strchr(pos+1, '/');
    }

    char* pathbreak = strrchr(in, '\\');
    if (!pathbreak) pathbreak = in-1;  // No path before filename

    if (pathbreak+1 == in+strlen(in)) {  // No filename - exit with error
        fprintf(stderr, "Error: The specified file name only contains a path.\n");
        exit(EXIT_FAILURE);
    }
    else {  // Create a copy of the file path without its extension
        char* extbreak = strrchr(in, '.');
        if (!extbreak || extbreak < pathbreak) size = strlen(in);  // File has no extension
        else size = extbreak - in;
        *out = (char*)emalloc(size + 1);
        strncpy(*out, in, size);
        (*out)[size] = '\0';
    }

    return;
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        printf("Usage: driver-music-extractor <filename>, or drag & drop the file onto the exe.\n");
        return 1;
    }
    else if (argc > 2) {
        printf("Error: Too many arguments.\n");
        return -1;
    }
    char* outputPath;
    getFilepath(argv[1], &outputPath);

    int offsets[17];
    short moduleHead[168];
    unsigned char* iData;
    unsigned char* oData;
    short* sData;
    int writePos;
    short rowCount;
    unsigned char patternHeader[9];
    short iPatternSize;
    short oPatternSize;
    short channelCount;
    unsigned char curChannel;
    short patternCount;
    short instCount;
    unsigned char vCount;
    int sampleCount;
    int moduleReadPos;
    int* soundbankList;
    int baseOffset;
    int lTmp;

    int instrumentSize;
    int sampleOffset;
    int sampleSize;
    int loopFlag;
    int numSamples;
    unsigned char sampleHeader[28];
    int loopStart;
    int loopLength;

    ifstream iFile;
    ofstream oFile;
    iFile.open(argv[1], ios::binary);
    if (!iFile.is_open()) {
        cout << "Unable to open the file." << endl << "Press any key to quit." << endl;
        getchar();
        return -1;
    }
    CreateDirectoryA(outputPath, NULL);  // Create the main directory

    iFile.read((char*) &offsets, 68);

    for (int musicFiles=0; musicFiles<=7; musicFiles++) {
        char oFileName[256];
        sprintf(oFileName, "%s\\Music %d.xm", outputPath, (musicFiles + 1));
        oFile.open(oFileName, ios::binary);
        if (!oFile.is_open()) {
            iFile.close();
            cout << "Unable to open output file." << endl << "Press any key to quit." << endl;
            getchar();
            return -1;
        }

        // Process & fix the module header
        moduleReadPos = offsets[musicFiles<<1];
        iFile.seekg(moduleReadPos, ios::beg);
        iFile.read((char*) &moduleHead, 336);
        moduleReadPos += 336;
        channelCount = moduleHead[34];
        patternCount = moduleHead[35];
        instCount = moduleHead[36];
        moduleHead[29] = 0x104;
        oFile.write((char *) &moduleHead, 336);

        // Convert the pattern data into the proper format
        for (int i=0; i<patternCount; i++) {
            iFile.read((char*) &patternHeader, 9);
            rowCount = ((short)patternHeader[6] << 8) + (short)patternHeader[5];
            iPatternSize = ((short)patternHeader[8] << 8) + (short)patternHeader[7];
            moduleReadPos += iPatternSize + 9;
            iData = new unsigned char[iPatternSize];
            iFile.read((char*) iData, iPatternSize);
            oPatternSize = validatePattern(iData, iPatternSize, channelCount, rowCount);
            if (oPatternSize > -1) {
                oData = new unsigned char[oPatternSize];
                curChannel = 0;
                writePos = 0;
                for (int k=0; k<iPatternSize; k++) {
                    if (iData[k] == 0xff) {
                        if (curChannel < channelCount) {
                            for (int j=1; j<=(channelCount - curChannel); j++) oData[writePos++] = 0x80;
                        }

                        curChannel = 0;
                        continue;
                    }

                    if ((iData[k] - curChannel) > 0) {
                        for (int j=1; j<=(iData[k] - curChannel); j++) oData[writePos++] = 0x80;
                        curChannel = iData[k];
                    }

                    k++;

                    vCount = 1;
                    if (iData[k] & 0x80) {
                        vCount += (iData[k] & 1) + ((iData[k] >> 1) & 1) + ((iData[k] >> 2) & 1) + ((iData[k] >> 3) & 1) + ((iData[k] >> 4) & 1);
                    }
                    else {
                        vCount += 4;
                    }

                    for (int j=k; j<(k+vCount); j++) oData[writePos++] = iData[j];

                    k += vCount - 1;
                    curChannel++;
                }
            }
            else {
                oPatternSize = rowCount * channelCount;
                oData = new unsigned char[oPatternSize];
                for (int k=0; k<oPatternSize; k++) oData[k] = 0x80;
            }

            patternHeader[7] = oPatternSize & 0xff;
            patternHeader[8] = (oPatternSize >> 8) & 0xff;
            oFile.write((char*) &patternHeader, 9);
            oFile.write((char*) oData, oPatternSize);
            delete[] iData;
            delete[] oData;
        }

        // Read the list of samples stored in the soundbank.
        iFile.seekg(offsets[(musicFiles<<1)+1], ios::beg);
        iFile.read((char*) &sampleCount, sizeof(int));
        soundbankList = new int[sampleCount << 2];
        iFile.read((char*) soundbankList, sizeof(int) * (sampleCount << 2));

        if (sampleCount < instCount) instCount = sampleCount;
        baseOffset = offsets[(musicFiles<<1)+1] + sampleCount * 16 + 4;

        // Decode & put the samples back into the module's instruments
        for (int i=0; i<instCount; i++) {
            numSamples = 0;
            loopStart = 0;
            loopLength = 0;
            iFile.seekg(moduleReadPos, ios::beg);
            iFile.read((char*) &instrumentSize, sizeof(int));
            iData = new unsigned char[instrumentSize];
            iFile.seekg(moduleReadPos, ios::beg);
            iFile.read((char*) iData, instrumentSize);
            moduleReadPos += instrumentSize;
            iData[26] = 0;
            oFile.write((char*) iData, instrumentSize);
            if (iData[27] == 0) continue;

            moduleReadPos += 12;
            sampleOffset = soundbankList[i<<2];
            sampleSize = soundbankList[(i<<2)+1];
            loopFlag = soundbankList[(i<<2)+2];
            iFile.seekg(baseOffset + sampleOffset + sampleSize - 16, ios::beg);  // These couple of lines deal with removal of padding & detecting empty samples. It's not perfect but it's good enough.
            iFile.read((char*) &lTmp, sizeof(int));
            lTmp = (lTmp >> 8) & 0xff;
            if ((loopFlag == 0) || ((loopFlag == 1) && (lTmp == 0))) sampleSize -= 16;
            if (sampleSize == 0) goto SkipSample;
            delete[] iData;
            iData = new unsigned char[sampleSize];
            numSamples = (sampleSize >> 4) * 28;
            sData = new short[numSamples];

            iFile.seekg(baseOffset + sampleOffset, ios::beg);
            iFile.read((char*) iData, sampleSize);
            decodeSample(iData, &sData, sampleSize, &loopStart, &loopLength);  // Decode the sample.

SkipSample:  // Write the sample header & the decoded sample data.
            lTmp = numSamples << 1;
            loopStart <<= 1;
            loopLength <<= 1;
            oFile.write((char*) &lTmp, sizeof(int));
            oFile.write((char*) &loopStart, sizeof(int));
            oFile.write((char*) &loopLength, sizeof(int));

            iFile.seekg(moduleReadPos, ios::beg);
            iFile.read((char*) &sampleHeader, 28);
            sampleHeader[5] = 0;
            oFile.write((char*) &sampleHeader, 28);
            if (sampleSize > 0) {
                oFile.write((char*) sData, sizeof(short) * numSamples);
                delete[] sData;
            }
            delete[] iData;
            moduleReadPos += 28;
        }

        lTmp = offsets[(musicFiles<<1)+1] - moduleReadPos;  //Attach any remaining data after the last sample has been processed.
        if (lTmp > 0) {
            iData = new unsigned char[lTmp];
            iFile.read((char*) iData, lTmp);
            oFile.write((char*) iData, lTmp);
            delete[] iData;
        }
        oFile.close();
        oFile.clear();
    }
    iFile.close();
    return 0;
}
