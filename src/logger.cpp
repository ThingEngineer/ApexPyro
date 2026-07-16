/*
 * Licensed under the Apache License 2.0. See LICENSE.
 * ApexPyro controls pyrotechnic hardware and is provided as-is, without warranty.
 * Compiling, flashing, and operating this software is entirely at your own risk.
 */

#define APX_USE_RAW_SERIAL
#include "logger.h"

#include <cstdarg>
#include <cstdio>

namespace {
const size_t LOG_PRINTF_BUFFER_SIZE = 320;
}

ApexSerialLogger apexSerial;

ApexSerialLogger::ApexSerialLogger()
    : serialPort(&::Serial), capturedSeq{}, writeIndex(0), lineCount(0), nextSequence(0) {
}

void ApexSerialLogger::begin(unsigned long baudRate) {
    serialPort->begin(baudRate);
}

size_t ApexSerialLogger::print(const String& value) {
    return writeText(value.c_str());
}

size_t ApexSerialLogger::print(const char* value) {
    return writeText(value ? value : "");
}

size_t ApexSerialLogger::print(char value) {
    const char buffer[2] = {value, '\0'};
    return writeText(buffer);
}

size_t ApexSerialLogger::println() {
    return writeText("\n");
}

size_t ApexSerialLogger::println(const String& value) {
    size_t written = writeText(value.c_str());
    written += println();
    return written;
}

size_t ApexSerialLogger::println(const char* value) {
    size_t written = writeText(value ? value : "");
    written += println();
    return written;
}

size_t ApexSerialLogger::printf(const char* format, ...) {
    char buffer[LOG_PRINTF_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length <= 0) {
        return 0;
    }

    return writeText(buffer);
}

uint32_t ApexSerialLogger::latestSequence() const {
    return nextSequence;
}

size_t ApexSerialLogger::collectRecent(String* linesOut, uint32_t* seqOut, size_t maxLines) const {
    if (maxLines == 0 || lineCount == 0) {
        return 0;
    }

    size_t requested = maxLines < lineCount ? maxLines : lineCount;
    size_t startOffset = lineCount - requested;
    size_t startIndex = (writeIndex + MAX_CAPTURED_LINES - lineCount + startOffset) % MAX_CAPTURED_LINES;

    for (size_t i = 0; i < requested; i++) {
        size_t index = (startIndex + i) % MAX_CAPTURED_LINES;
        linesOut[i] = capturedLines[index];
        seqOut[i] = capturedSeq[index];
    }

    return requested;
}

size_t ApexSerialLogger::collectSince(uint32_t afterSequence, String* linesOut, uint32_t* seqOut, size_t maxLines) const {
    if (maxLines == 0 || lineCount == 0) {
        return 0;
    }

    size_t count = 0;
    size_t startIndex = (writeIndex + MAX_CAPTURED_LINES - lineCount) % MAX_CAPTURED_LINES;
    for (size_t i = 0; i < lineCount && count < maxLines; i++) {
        size_t index = (startIndex + i) % MAX_CAPTURED_LINES;
        if (capturedSeq[index] <= afterSequence) {
            continue;
        }

        linesOut[count] = capturedLines[index];
        seqOut[count] = capturedSeq[index];
        count++;
    }

    return count;
}

size_t ApexSerialLogger::writeText(const char* text) {
    const char* safeText = text ? text : "";
    size_t written = serialPort->print(safeText);
    captureText(safeText);
    return written;
}

void ApexSerialLogger::captureText(const char* text) {
    if (!text) {
        return;
    }

    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        char ch = *cursor;
        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            commitLine(partialLine);
            partialLine = "";
            continue;
        }

        if (partialLine.length() < MAX_LINE_LENGTH) {
            partialLine += ch;
        }
    }
}

void ApexSerialLogger::commitLine(const String& line) {
    capturedLines[writeIndex] = line;
    capturedSeq[writeIndex] = ++nextSequence;
    writeIndex = (writeIndex + 1) % MAX_CAPTURED_LINES;
    if (lineCount < MAX_CAPTURED_LINES) {
        lineCount++;
    }
}