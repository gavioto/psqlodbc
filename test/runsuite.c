/*
 * A test driver for the psqlodbc regression tests.
 *
 * This program runs one regression tests from the src/ directory,
 * and compares the output with the expected output in the expected/ directory.
 * Reports success or failure in TAP compatible fashion.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef WIN32
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define strdup _strdup
#endif

static int rundiff(const char *testname);
static int runtest(const char *binname, const char *testname, int testno);

static char *slurpfile(const char *filename, size_t *len);

static void
bailout(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);

	printf("Bail out! ");
	vprintf(fmt, argp);

	va_end(argp);

	exit(1);
}

#ifdef WIN32
#define DIR_SEP '\\'
#else
#define DIR_SEP '/'
#endif

/* Given a test program's name, get the test name */
void
parse_argument(const char *in, char *testname, char *binname)
{
	const char *basename;
#ifdef WIN32
	const char *suffix = "-test.exe";
#else
	const char *suffix = "-test";
#endif
	size_t		len;

	/* if the input is a plain test name, construct the binary name from it */
	if (strchr(in, DIR_SEP) == NULL)
	{
		strcpy(testname, in);
		sprintf(binname, "src%c%s-test", DIR_SEP, in);
		return;
	}

	/*
	 * Otherwise the input is a binary name, and we'll construct the test name
	 * from it.
	 */
	strcpy(binname, in);

	/* Find the last / or \ character */
	basename = strrchr(in, DIR_SEP) + 1;

	/* Strip -test or -test.exe suffix */
	if (strlen(basename) <= strlen(suffix))
	{
		strcpy(testname, basename);
		return;
	}

	len = strlen(basename) - strlen(suffix);
	if (strcmp(&basename[len], suffix) != 0)
	{
		strcpy(testname, basename);
		return;
	}

	memcpy(testname, basename, len);
	testname[len] = '\0';
}

int main(int argc, char **argv)
{
	char		binname[1000];
	char		testname[100];
	int			numtests;
	int			i;
	int			failures;

	if (argc < 2)
	{
		printf("Usage: runsuite <test binary> ...\n");
		exit(1);
	}
	numtests = argc - 1;

	printf("TAP version 13\n");
	printf("1..%d\n", numtests);

	/*
	 * We accept either test binary name or plain test name.
	 */
	failures = 0;
	for (i = 1; i <= numtests; i++)
	{
		parse_argument(argv[i], testname, binname);
		if (runtest(binname, testname, i) != 0)
			failures++;
	}

	exit(failures > 254 ? 254 : failures);
}

/* Return 0 on success, 1 on failure */
static int
runtest(const char *binname, const char *testname, int testno)
{
	char		cmdline[1024];
	int			rc;
	int			ret;
	int			diff;

	/*
	 * ODBCSYSINI=. tells unixodbc where to find the driver config file,
	 * odbcinst.ini
	 *
	 * ODBCINSTINI=./odbcinst.ini tells the same for iodbc. iodbc also requires
	 * ODBCINI=./odbc.ini to tell it where to find the datasource config.
	 *
	 * We wouldn't need to iodbc stuff when building with unixodbc and vice
	 * versa, but it doesn't hurt either.
	 */
#ifndef WIN32
	snprintf(cmdline, sizeof(cmdline),
			 "ODBCSYSINI=. ODBCINSTINI=./odbcinst.ini ODBCINI=./odbc.ini "
			 "%s > results/%s.out",
			 binname, testname);
#else
	snprintf(cmdline, sizeof(cmdline),
			 "%s > results\\%s.out",
			 binname, testname);
#endif
	rc = system(cmdline);

	diff = rundiff(testname);
	if (rc != 0)
	{
		printf("not ok %d - %s test returned %d\n", testno, testname, rc);
		ret = 1;
	}
	else if (diff != 0)
	{
		printf("not ok %d - %s test output differs\n", testno, testname);
		ret = 1;
	}
	else
	{
		printf("ok %d - %s\n", testno, testname);
		ret = 0;
	}
	fflush(stdout);

	return ret;
}

static int
rundiff(const char *testname)
{
	char		filename[1024];
	char		cmdline[1024];
	int			outputno;
	char	   *result;
	size_t		result_len;

	snprintf(filename, sizeof(filename), "results/%s.out", testname);
	result = slurpfile(filename, &result_len);

	outputno = 0;
	for (;;)
	{
		char	   *expected;
		size_t		expected_len;

		if (outputno == 0)
			snprintf(filename, sizeof(filename), "expected/%s.out", testname);
		else
			snprintf(filename, sizeof(filename), "expected/%s_%d.out", testname, outputno);
		expected = slurpfile(filename, &expected_len);
		if (expected == NULL)
		{
			if (outputno == 0)
				bailout("could not open file %s: %s\n", filename, strerror(ENOENT));
			free(result);
			break;
		}

		if (expected_len == result_len &&
			memcmp(expected, result, expected_len) == 0)
		{
			/* The files are equal. */
			free(result);
			free(expected);
			return 0;
		}

		free(expected);

		outputno++;
	}
	/* no matching output found */

	/*
	 * Try to run diff. If this fails, e.g. because the 'diff' program is
	 * not installed, which is typical on Windows system, that's OK. You'll
	 * miss the regression.diffs output, but we'll still report "not ok"
	 * correctly. You can always compare the files manually...
	 *
	 * XXX: Somewhat arbitrarily, always run the diff against the primary
	 * expected output file. Perhaps we should run it against all output
	 * files and print the smallest diff?
	 */
	snprintf(cmdline, sizeof(cmdline),
			 "diff -c expected/%s.out results/%s.out >> regression.diffs",
			 testname, testname);
	if (system(cmdline) == -1)
		printf("# diff failed\n");

	return 1;
}


/*
 * Reads file to memory. The file is returned, or NULL if it doesn't exist.
 * Length is returned in *len.
 */
static char *
slurpfile(const char *filename, size_t *len)
{
	int			fd;
	struct stat stbuf;
	int			readlen;
	off_t		filelen;
	char	   *p;

#ifdef WIN32
	fd = open(filename, O_RDONLY | O_BINARY, 0);
#else
	fd = open(filename, O_RDONLY, 0);
#endif
	if (fd == -1)
	{
		if (errno == ENOENT)
			return NULL;

		bailout("could not open file %s: %s\n", filename, strerror(errno));
	}
	if (fstat(fd, &stbuf) < 0)
		bailout("fstat failed on file %s: %s\n", filename, strerror(errno));

	filelen = stbuf.st_size;
	p = malloc(filelen + 1);
	if (!p)
		bailout("out of memory reading file %s\n", filename);
	readlen = read(fd, p, filelen);
	if (readlen != filelen)
		bailout("read only %d bytes out of %d from %s\n", (int) readlen, (int) filelen, filename);
	p[readlen] = '\0';
	close(fd);

	*len = readlen;
	return p;
}
