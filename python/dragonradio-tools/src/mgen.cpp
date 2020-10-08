
#include "mgen.hh"

static Send parseSendRecord(Tok &field_tok);
static Recv parseRecvRecord(Tok &field_tok);
static datetime64ns parseTimestamp(char *timestamp);
static void parseIPAndPort(char *ip_port, in_addr_t &ip, uint16_t &port);
static void parseIP(char *ipstr, in_addr_t &ip);

template<class T>
std::vector<T> parse(const char *path, const char *type, std::function<T(Tok &)> p)
{
    const size_t   kMaxLineLength = 4096;
    char           line[kMaxLineLength];
    std::ifstream  fs;
    std::vector<T> records;

    fs.open(path);

    while (fs) {
        fs.getline(line, kMaxLineLength);

        // Look for timestamp
        Tok  tok(line, " ");
        char *timestampstr = *tok;
        char *rtypestr = tok.next(" ");

        // Look for timestamp and record type
        if (!timestampstr || !rtypestr)
            continue;

        // Handle record
        if (!strcmp(rtypestr, type)) {
            T rec = p(tok);

            rec.timestamp = static_cast<int64_t>(parseTimestamp(timestampstr));
            records.push_back(rec);
        }
    }

    return records;
}

std::vector<Send> parseSend(const char *path)
{
    return parse<Send>(path, "SEND", parseSendRecord);
}

std::vector<Recv> parseRecv(const char *path)
{
    return parse<Recv>(path, "RECV", parseRecvRecord);
}

static Send parseSendRecord(Tok &field_tok)
{
    Send rec;

    while (*field_tok) {
        Tok  keyval_tok(*field_tok, ">");
        char *key = *keyval_tok;
        char *val = keyval_tok.next("");

        if (key && val) {
            if (!strcmp(key, "flow"))
                rec.flow = atoi(val);
            else if (!strcmp(key, "seq"))
                rec.seq = atoi(val);
            else if (!strcmp(key, "frag"))
                rec.frag = atoi(val);
            else if (!strcmp(key, "TOS"))
                rec.tos = atoi(val);
            else if (!strcmp(key, "srcPort"))
                rec.src_port = atoi(val);
            else if (!strcmp(key, "dst"))
                parseIPAndPort(val, rec.dest_ip, rec.dest_port);
            else if (!strcmp(key, "size"))
                rec.size = atoi(val);
        }

        field_tok.next(" ");
    }

    return rec;
}

static Recv parseRecvRecord(Tok &field_tok)
{
    Recv rec;

    while (*field_tok) {
        Tok  keyval_tok(*field_tok, ">");
        char *key = *keyval_tok;
        char *val = keyval_tok.next("");

        if (key && val) {
            if (!strcmp(key, "flow"))
                rec.flow = atoi(val);
            else if (!strcmp(key, "seq"))
                rec.seq = atoi(val);
            else if (!strcmp(key, "frag"))
                rec.frag = atoi(val);
            else if (!strcmp(key, "TOS"))
                rec.tos = atoi(val);
            else if (!strcmp(key, "src"))
                parseIPAndPort(val, rec.src_ip, rec.src_port);
            else if (!strcmp(key, "dst"))
                parseIPAndPort(val, rec.dest_ip, rec.dest_port);
            else if (!strcmp(key, "sent"))
                rec.sent = static_cast<int64_t>(parseTimestamp(val));
            else if (!strcmp(key, "size"))
                rec.size = atoi(val);
        }

        field_tok.next(" ");
    }

    return rec;
}

/** @brief Parse a timestamp
 * @param timestamp The timestamp
 */
/** Timestamps have the form:
 *    2020-09-30_22:38:38.847413
 */
static datetime64ns parseTimestamp(char *timestamp)
{
    struct tm time = {0};
    int64_t   ns;

    // Split timestamp into date and time
    Tok  date_time_tok(timestamp, "_");
    char *datestr = *date_time_tok;
    char *timestr = date_time_tok.next("");

    if (!datestr || !timestr)
        throw std::domain_error("Cannot parse timestamp");

    // Parse date
    Tok  date_tok(datestr, "-");
    char *yearstr = *date_tok;
    char *monthstr = date_tok.next("-");
    char *daystr = date_tok.next("");

    if (!yearstr || !monthstr || !daystr)
        throw std::domain_error("Cannot parse timestamp");

    time.tm_year = atoi(yearstr) - 1900;
    time.tm_mon = atoi(monthstr) - 1;
    time.tm_mday = atoi(daystr);

    // Parse time
    Tok  time_tok(timestr, ":");
    char *hourstr = *time_tok;
    char *minstr = time_tok.next(":");
    char *secstr = time_tok.next(".");
    char *fracstr = time_tok.next("");

    if (!hourstr || !minstr || !secstr || !fracstr)
        throw std::domain_error("Cannot parse timestamp");

    time.tm_hour = atoi(hourstr);
    time.tm_min = atoi(minstr);
    time.tm_sec = atoi(secstr);
    fracstr[-1] = '.';
    ns = atof(fracstr-1)*1000000000.0;

    return datetime64ns(time, ns);
}

/** @brief Parse IP and port
 * @param ip_port The IP and port
 * @param ip Destination for parsed IP
 * @param port Destination for parsed port
 */
/** An IP and port has the form:
 *    192.168.126.5/5017
 */
static void parseIPAndPort(char *ip_port, in_addr_t &ip, uint16_t &port)
{
    Tok ip_tok(ip_port, "/");
    char *ipstr = *ip_tok;
    char *portstr = ip_tok.next("");

    if (!ipstr || !portstr)
        throw std::domain_error("Cannot parse IP and port");

    parseIP(ipstr, ip);
    port = atoi(portstr);
}

static void parseIP(char *ipstr, in_addr_t &ip)
{
    Tok ip_tok(ipstr, ".");

    ip = 0;

    for (int i = 0; i < 4; ++i) {
        if (!ip_tok)
            throw std::domain_error("Cannot parse IP");

        ip <<= 8;
        ip += atoi(*ip_tok);

        ip_tok.next(i == 2 ? "" : ".");
    }
}