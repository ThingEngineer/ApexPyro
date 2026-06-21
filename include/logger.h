#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

class ApexSerialLogger {
public:
    ApexSerialLogger();

    void begin(unsigned long baudRate);

    size_t print(const String& value);
    size_t print(const char* value);
    size_t print(char value);

    template <typename T>
    size_t print(const T& value) {
        return print(String(value));
    }

    size_t println();
    size_t println(const String& value);
    size_t println(const char* value);

    template <typename T>
    size_t println(const T& value) {
        size_t written = print(value);
        written += println();
        return written;
    }

    size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3)));

    uint32_t latestSequence() const;
    size_t collectRecent(String* linesOut, uint32_t* seqOut, size_t maxLines) const;
    size_t collectSince(uint32_t afterSequence, String* linesOut, uint32_t* seqOut, size_t maxLines) const;

private:
    static const size_t MAX_CAPTURED_LINES = 160;
    static const size_t MAX_LINE_LENGTH = 240;

    HardwareSerial* serialPort;
    String capturedLines[MAX_CAPTURED_LINES];
    uint32_t capturedSeq[MAX_CAPTURED_LINES];
    size_t writeIndex;
    size_t lineCount;
    uint32_t nextSequence;
    String partialLine;

    size_t writeText(const char* text);
    void captureText(const char* text);
    void commitLine(const String& line);
};

extern ApexSerialLogger apexSerial;

#ifndef APX_USE_RAW_SERIAL
#define Serial apexSerial
#endif

#endif