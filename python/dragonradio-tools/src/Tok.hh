#ifndef TOK_HH_
#define TOK_HH_

#include <string.h>

class Tok {
public:
    Tok(char *str, const char *delim)
      : result_(strtok_r(str, delim, &saveptr_))
    {
    }

    Tok() = delete;
    ~Tok() = default;

    operator bool()
    {
        return result_ != NULL;
    }

    char *operator *()
    {
        return result_;
    }

    char *next(const char *delim)
    {
        result_ = strtok_r(NULL, delim, &saveptr_);
        return result_;
    }

private:
    char *result_;

    char *saveptr_;
};

#endif /* TOK_HH_ */