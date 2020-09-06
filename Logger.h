#include <Arduino.h>

class Logger
{
    String _log;
    long time;
    String lastMessage;

    public:
        void print(String msg)
        {
            Serial.print(msg);
            _log += msg;
        }
        void println(String msg)
        {
            Serial.println(msg);
            _log += msg + "\n";
        }

        String GetLog(int buffer)
        {
            return _log + "\nBuffer: " + (String)buffer + "\n" + (String)time + lastMessage;
        }

        void SetLastMessage(String msg, long _time)
        {
            Serial.print(msg);
            time = _time;
            lastMessage = msg;
        }

        void AppendLastMessage(String msg)
        {
            Serial.print(msg);
            lastMessage += msg;
        }
};
