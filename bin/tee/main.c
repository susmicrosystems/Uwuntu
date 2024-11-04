#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <errno.h>

static void usage(const char *progname)
{
	printf("%s [-h] [-a] FILES\n", progname);
	printf("-h: show this help\n");
	printf("-a: open the given files in append mode\n");
}

int main(int argc, char **argv)
{
	int c;
	int append = 0;
	FILE *files[4096];
	size_t files_count = 0;

	while ((c = getopt(argc, argv, "ah")) != -1)
	{
		switch (c)
		{
			case 'a':
				append = 1;
				break;
			case 'h':
				usage(argv[0]);
				return EXIT_SUCCESS;
			default:
				usage(argv[0]);
				return EXIT_FAILURE;
		}
	}
	if (argc - optind >= (int)(sizeof(files) / sizeof(*files)))
	{
		fprintf(stderr, "%s: too much files\n", argv[0]);
		return EXIT_FAILURE;
	}
	for (int i = optind; i < argc; ++i)
	{
		files[files_count] = fopen(argv[i], append ? "ab" : "wb");
		if (!files[files_count])
			fprintf(stderr, "%s: fopen(%s): %s\n", argv[0], argv[i],
			        strerror(errno));
		else
			files_count++;
	}
	while (1)
	{
		uint8_t buf[4096];
		size_t rd = fread(buf, 1, sizeof(buf), stdin);
		if (ferror(stdin))
		{
			fprintf(stderr, "%s: fread: %s\n", argv[0], strerror(errno));
			return EXIT_FAILURE;
		}
		fwrite(buf, 1, rd, stdout);
		for (size_t i = 0; i < files_count; ++i)
			fwrite(buf, 1, rd, files[i]);
		if (feof(stdin))
			break;
	}
	return EXIT_SUCCESS;
}
