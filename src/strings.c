#define _GNU_SOURCE
#include <ctype.h>
#include <string.h>

#include "strings.h"

/**
 * Check if comment character is inside a quoted sequence.
 *
 * @param s The string to be trimmed.
 * @param p Position of comment character in string.
 * @return char* Position of comment character or character after quoted sequence containing it.
 * */
static char* is_comment_in_quoted_sequence(char* s, char* p, char quote)
{
    char* q = strchr(s, quote);
    if (q != NULL && q < p)
    {
        q = strchr(q + 1, quote);
        if (q != NULL && q > p) return q + 1;
    }
    return p;
}

/**
 * Trims trailing comments from a string.
 *
 * @param s The string to be trimmed.
 * @return char* The string without any trailing comments.
 * */
char* trim_comment(char* s)
{
    if (s != NULL)
    {
        char* p = strchr(s, '#');
        if (p != NULL)
        {
            char* q1 = is_comment_in_quoted_sequence(s, p, '"');
            char* q2 = is_comment_in_quoted_sequence(s, p, '\'');
            char* q = (q1 > q2 ? q1 : q2); // Get outer quoted sequence
            if (q > p)
            {
                // '#' is inside a string, try again after the quoted sequence
                trim_comment(q);
                return s;
            }

            // p points to the start of the comment.
            *p = '\0';
        }
    }
    return s;
}

/**
 * Right trims a string.
 *
 * @param s The string to be trimmed.
 * @remarks
 * Credit to chux: https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way#122721
 * */
char* rtrim_string(char* s)
{
    char* p = s;
    while (*p) p++;
    p--;
    while (p >= s && isspace((unsigned char)*p)) p--;
    p[1] = '\0';
    return s;
}

/**
 * Trims a string.
 *
 * @param s The string to be trimmed.
 * @remarks
 * Credit to chux: https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way#122721
 * */
char* trim_string(char* s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s)
    {
        char* p = s;
        while (*p) p++;
        while (isspace((unsigned char)*(--p)));
        p[1] = '\0';
    }
    return s;
}

/**
 * Checks if a string starts with a specific substring.
 *
 * @param s The string to be inspected.
 * @param ss The substring to search for.
 */
int starts_with(const char* s, const char* ss)
{
    return strncmp(s, ss, strlen(ss)) == 0;
}

/**
 * Checks for commented or empty lines.
 *
 * @param line The configuration file line.
 */
int is_comment_or_empty(char* line)
{
    return line[0] == '#' || line[0] == '\0';
}
