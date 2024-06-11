/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017 Intel Corporation
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#include <stddef.h>
#include <sys/queue.h>

#include <rte_common.h>
#include <rte_hexdump.h>
#include <rte_log.h>

#define MAX_BURST 512U
#define DEFAULT_BURST 32U
#define DEFAULT_OPS 64U
#define DEFAULT_ITER 6U

enum op_data_type {
  DATA_INPUT = 0,
  DATA_SOFT_OUTPUT,
  DATA_HARD_OUTPUT,
  DATA_HARQ_INPUT,
  DATA_HARQ_OUTPUT,
  DATA_NUM_TYPES,
};

struct unit_test_case {
	int (*setup)(void);
	void (*teardown)(void);
	int (*testcase)(void);
	const char *name;
};

#define TEST_CASE(testcase) {NULL, NULL, testcase, #testcase}

#define TEST_CASE_ST(setup, teardown, testcase) \
		{setup, teardown, testcase, #testcase}

#define TEST_CASES_END() {NULL, NULL, NULL, NULL}

struct unit_test_suite {
	const char *suite_name;
	int (*setup)(void);
	void (*teardown)(void);
	struct unit_test_case unit_test_cases[];
};

int unit_test_suite_runner(struct unit_test_suite *suite);

typedef int (test_callback)(void);
TAILQ_HEAD(test_commands_list, test_command);
struct test_command {
	TAILQ_ENTRY(test_command) next;
	const char *command;
	test_callback *callback;
};

void add_test_command(struct test_command *t);

/* Register a test function */
#define REGISTER_TEST_COMMAND(name, testsuite) \
	static int test_func_##name(void) \
	{ \
		return unit_test_suite_runner(&testsuite); \
	} \
	static struct test_command test_struct_##name = { \
		.command = RTE_STR(name), \
		.callback = test_func_##name, \
	}; \
	RTE_INIT(test_register_##name) \
	{ \
		add_test_command(&test_struct_##name); \
	}

const char *get_vector_filename(void);

unsigned int get_num_ops(void);

unsigned int get_burst_sz(void);

unsigned int get_num_lcores(void);

double get_snr(void);

unsigned int get_iter_max(void);

bool get_init_device(void);

#endif
