#ifndef _REPLY_TO_CALLER_H
#define _REPLY_TO_CALLER_H

namespace Example
{
    /// Execute the reply-to-caller example: an async service delivers each
    /// completion on the thread that made the request, found with
    /// dmq::ThisThread::GetCurrent().
    void ReplyToCallerExample();
}

#endif
