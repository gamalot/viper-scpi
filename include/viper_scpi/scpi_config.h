/**
 * @file scpi_config.h
 * @brief Compile-time limits and defaults for viper-scpi.
 *
 * Every macro can be overridden on the compiler command line
 * (e.g. -DVIPER_SCPI_BUFFER_SIZE=256) before this header is included.
 */
#ifndef VIPER_SCPI_CONFIG_H
#define VIPER_SCPI_CONFIG_H

/** Size of the line buffer in bytes; one program message must fit in it. */
#ifndef VIPER_SCPI_BUFFER_SIZE
#define VIPER_SCPI_BUFFER_SIZE 128
#endif

/** Maximum number of parameters accepted per command. */
#ifndef VIPER_SCPI_MAX_PARAMS
#define VIPER_SCPI_MAX_PARAMS 8
#endif

/** Maximum number of nodes in one command header (e.g. A:B:C? has 3). */
#ifndef VIPER_SCPI_MAX_RECURSION_DEPTH
#define VIPER_SCPI_MAX_RECURSION_DEPTH 8
#endif

/** Maximum number of numeric suffixes recorded per command header. */
#ifndef VIPER_SCPI_MAX_SUFFIXES
#define VIPER_SCPI_MAX_SUFFIXES 4
#endif

/**
 * Significant digits of viper_scpi_result_double() (1..17). 15 is the most
 * that any decimal value survives text -> double -> text with, so a setting
 * such as VOLT 1.23456789012 reads back exactly as written, without the noise
 * digits 17 would add (0.1 -> 0.10000000000000001). Use 17 for a lossless
 * round trip of computed doubles, or viper_scpi_result_double_digits().
 */
#ifndef VIPER_SCPI_DOUBLE_DIGITS
#define VIPER_SCPI_DOUBLE_DIGITS 15
#endif

/** Number of entries in the SCPI error queue (must be 2..255). */
#ifndef VIPER_SCPI_ERROR_QUEUE_SIZE
#define VIPER_SCPI_ERROR_QUEUE_SIZE 8
#endif

/** Default *IDN? fields used when the configuration leaves them NULL. */
#ifndef VIPER_SCPI_DEFAULT_MANUFACTURER
#define VIPER_SCPI_DEFAULT_MANUFACTURER "VIPER"
#endif
#ifndef VIPER_SCPI_DEFAULT_MODEL
#define VIPER_SCPI_DEFAULT_MODEL "SCPI"
#endif
#ifndef VIPER_SCPI_DEFAULT_SERIAL
#define VIPER_SCPI_DEFAULT_SERIAL "0"
#endif
#ifndef VIPER_SCPI_DEFAULT_FIRMWARE
#define VIPER_SCPI_DEFAULT_FIRMWARE "0.1.0"
#endif

/* The documented ranges, checked: the context stores these counts in 8 bits,
 * and a value outside them would corrupt state without a warning. */
#if VIPER_SCPI_BUFFER_SIZE < 1 || VIPER_SCPI_BUFFER_SIZE > 65535
#error "VIPER_SCPI_BUFFER_SIZE must be 1..65535"
#endif
#if VIPER_SCPI_MAX_PARAMS < 1 || VIPER_SCPI_MAX_PARAMS > 255
#error "VIPER_SCPI_MAX_PARAMS must be 1..255"
#endif
#if VIPER_SCPI_MAX_RECURSION_DEPTH < 1 || VIPER_SCPI_MAX_RECURSION_DEPTH > 255
#error "VIPER_SCPI_MAX_RECURSION_DEPTH must be 1..255"
#endif
#if VIPER_SCPI_MAX_SUFFIXES < 1 || VIPER_SCPI_MAX_SUFFIXES > 255
#error "VIPER_SCPI_MAX_SUFFIXES must be 1..255"
#endif
#if VIPER_SCPI_DOUBLE_DIGITS < 1 || VIPER_SCPI_DOUBLE_DIGITS > 17
#error "VIPER_SCPI_DOUBLE_DIGITS must be 1..17"
#endif
#if VIPER_SCPI_ERROR_QUEUE_SIZE < 2 || VIPER_SCPI_ERROR_QUEUE_SIZE > 255
#error "VIPER_SCPI_ERROR_QUEUE_SIZE must be 2..255"
#endif

#endif /* VIPER_SCPI_CONFIG_H */
