#include "common.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>

#include <zlib.h>

typedef struct Arguments {
  const char *input_filename;
  const char *output_filename;
  bool has_help;
} Arguments;

const char *const USAGE_FORMAT =
    "Usage: %s INPUT_FILE OUTPUT_FILE\n"
    "Decompress a zlib-compressed INPUT_FILE into OUTPUT_FILE.\n"
    "\n"
    " -h, --help               display this message and exit\n";

Error parse_arguments(int argc, char *argv[], Arguments *args);
Error do_decompress(z_stream *stream, bool *finished);

int main(int argc, char *argv[]) {
  Arguments args;
  Error error = parse_arguments(argc, argv, &args);

  if (error.what) {
    print_error(error);

    return EXIT_FAILURE;
  }

  if (args.has_help) {
    printf(USAGE_FORMAT, executable_name);

    return EXIT_SUCCESS;
  }

  FileAndMapping input_file;
  error = open_and_map_file(args.input_filename, &input_file);
  int return_code = EXIT_SUCCESS;

  if (error.what) {
    print_error(error);

    return EXIT_FAILURE;
  }

  FileAndMapping output_file;
  error =
      create_and_map_file(args.output_filename, input_file.size, &output_file);

  if (error.what) {
    print_error(error);
    return_code = EXIT_FAILURE;

    goto cleanup_input_only;
  }

  z_stream stream = {.next_in = NULL,
                     .avail_in = 0,
                     .zalloc = Z_NULL,
                     .zfree = Z_NULL,
                     .opaque = Z_NULL};

  const int init_errc = inflateInit(&stream);

  if (init_errc != Z_OK) {
    assert(init_errc != Z_STREAM_ERROR);

    return_code = EXIT_FAILURE;

    const char *what;
    switch (init_errc) {
    case Z_MEM_ERROR:
      what = "out of memory";

      break;

    case Z_VERSION_ERROR:
      what = "zlib library version mismatch";

      break;
    default:
      abort();
    }

    if (stream.msg) {
      error = eformat("couldn't initialize inflate stream: %s (%d): %s", what,
                      init_errc, stream.msg);
    } else {
      error = eformat("couldn't initialize inflate stream: %s (%d)", what,
                      init_errc);
    }

    print_error(error);

    goto cleanup;
  }

  error =
      transform_mapped_file(&input_file, &output_file, do_decompress, &stream);

  if (error.what) {
    print_error(error);
    return_code = EXIT_FAILURE;
  }

  const int inflate_end_errc = inflateEnd(&stream);
  assert(inflate_end_errc == Z_OK);

cleanup:
  error = free_file(output_file);

  if (error.what) {
    print_error(error);
    return_code = EXIT_FAILURE;
  }

cleanup_input_only:
  error = free_file(input_file);

  if (error.what) {
    print_error(error);
    return_code = EXIT_FAILURE;
  }

  return return_code;
}

static const struct option OPTIONS[] = {
    {.name = "help", .has_arg = no_argument, .flag = NULL, .val = 'h'}, {0}};

Error parse_arguments(int argc, char *argv[], Arguments *args) {
  assert(argc >= 1);
  assert(argv[0]);
  assert(args);

  executable_name = argv[0];
  opterr = false;

  bool has_help = false;

  while (true) {
    int option_index;
    const int option_char =
        getopt_long(argc, argv, "h", OPTIONS, &option_index);

    if (option_char == -1) {
      break;
    }

    switch (option_char) {
    case 'h':
      has_help = true;

      break;
    case '?':
      return eformat("unrecognized option '%s'", argv[optind - 1]);
    default:
      assert(false);
    }
  }

  const char *input_filename;
  if (optind >= argc) {
    if (!has_help) {
      return MAKE_ERROR("missing argument INPUT_FILE");
    } else {
      input_filename = NULL;
    }
  } else {
    input_filename = argv[optind];
  }

  const char *output_filename;
  if (optind >= argc - 1) {
    if (!has_help) {
      return MAKE_ERROR("missing argument OUTPUT_FILE");
    } else {
      output_filename = NULL;
    }
  } else {
    output_filename = argv[optind + 1];
  }

  *args = (Arguments){.input_filename = argv[optind],
                      .output_filename = argv[optind + 1],
                      .has_help = has_help};

  return NULL_ERROR;
}

Error do_decompress(z_stream *stream, bool *finished) {
  assert(stream);
  assert(finished);

  if (stream->avail_in == 0) {
    *finished = true;

    return NULL_ERROR;
  }

  int flag;

  if ((size_t)stream->avail_out / 1032 > (size_t)stream->avail_in) {
    flag = Z_FINISH;
  } else {
    flag = Z_NO_FLUSH;
  }

  const int errc = inflate(stream, flag);

  if (errc != Z_OK) {
    assert(errc != Z_STREAM_ERROR);
    assert(errc != Z_BUF_ERROR);

    const char *what;
    switch (errc) {
    case Z_STREAM_END:
      *finished = true;

      return NULL_ERROR;

    case Z_NEED_DICT:
      what = "dictionary needed";

      break;

    case Z_DATA_ERROR:
      what = "input data corrupted";

      break;

    case Z_MEM_ERROR:
      what = "out of memory";

      break;

    default:
      abort();
    }

    if (stream->msg) {
      return eformat("couldn't inflate stream: %s (%d): %s", what, errc,
                     stream->msg);
    }

    return eformat("couldn't inflate stream: %s (%d)", what, errc);
  }

  *finished = false;

  return NULL_ERROR;
}
