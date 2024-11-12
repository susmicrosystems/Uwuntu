#include <std.h>

int isalnum(int c)
{
	return (c >= 'a' && c <= 'z')
	    || (c >= 'A' && c <= 'Z')
	    || (c >= '0' && c <= '9');
}

int isalpha(int c)
{
	return (c >= 'a' && c <= 'z')
	    || (c >= 'A' && c <= 'Z');
}

int iscntrl(int c)
{
	return c < ' ' || c > '~';
}

int isdigit(int c)
{
	return c >= '0' && c <= '9';
}

int isgraph(int c)
{
	return c > ' ' && c <= '~';
}

int islower(int c)
{
	return c >= 'a' && c <= 'z';
}

int isprint(int c)
{
	return c >= ' ' && c <= '~';
}

int ispunct(int c)
{
	return (c >= '!' && c <= '/')
	    || (c >= ':' && c <= '@')
	    || (c >= '[' && c <= '`')
	    || (c >= '{' && c <= '~');
}

int isspace(int c)
{
	return c == ' '  || c == '\t'
	    || c == '\n' || c == '\f'
	    || c == '\r' || c == '\v';
}

int isupper(int c)
{
	return c >= 'A' && c <= 'Z';
}

int isxdigit(int c)
{
	return (c >= '0' && c <= '9')
	    || (c >= 'a' && c <= 'f')
	    || (c >= 'A' && c <= 'F');
}

int isascii(int c)
{
	return c >= 0 && c <= 127;
}

int isblank(int c)
{
	return c == ' ' || c == '\t';
}

int toupper(int c)
{
	if (c >= 'a' && c <= 'z')
		return c + 'A' - 'a';
	return c;
}

int tolower(int c)
{
	if (c >= 'A' && c <= 'Z')
		return c + 'a' - 'A';
	return c;
}
