#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <error.h>
#include "ldst.h"

#define ARR_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

static const uint8_t patt[16] = {
	0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87,
	0x78, 0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f,
};

static const uint64_t load_val[16] = {
	0x8796a5b4c3d2e1f0ULL,
	0x788796a5b4c3d2e1ULL,
	0x69788796a5b4c3d2ULL,
	0x5a69788796a5b4c3ULL,
	0x4b5a69788796a5b4ULL,
	0x3c4b5a69788796a5ULL,
	0x2d3c4b5a69788796ULL,
	0x1e2d3c4b5a697887ULL,
	0x0f1e2d3c4b5a6978ULL,
	  0x0f1e2d3c4b5a69ULL,
	    0x0f1e2d3c4b5aULL,
	      0x0f1e2d3c4bULL,
	        0x0f1e2d3cULL,
	          0x0f1e2dULL,
	            0x0f1eULL,
	              0x0fULL,
};

int main()
{
	unsigned i, j, k;
	//ldst_val val;

	// Test int read
	for (j = 0; j < ARR_SIZE(load_funcs); ++j) {
		const mem_op_desc *desc = load_funcs[j].desc;
		unsigned shift = 8 * (sizeof(long) - desc->len);

		for (i = 0; i < ARR_SIZE(patt) - desc->len; ++i) {
			unsigned long expected;
			if (desc->len != 1 && i % desc->len == 0)
				continue;

			expected = load_val[i];
			if (desc->signext)
				expected = (long)(expected << shift) >> shift;
			else
				expected = (expected << shift) >> shift;

			printf("load %c%u, insn %s, n=%u, off=%u, cmp=%lx\n",
				desc->signext ? 'i' : 'u',
				desc->len * 8, load_funcs[j].name, desc->n, i, expected);
			fflush(stdout);
			for (k = 0; k < desc->n; ++k) {
				unsigned long read = load_i(&patt[i], j, k);
				if (read != expected)
					error(1, 0, "failed at load_i %u %u %u: %lx",
						i, j, k, read);
			}
		}
	}
	// Test fp load
	for (j = 0; j < ARR_SIZE(loadfp_funcs); ++j) {
		const mem_op_desc *desc = loadfp_funcs[j].desc;

		for (i = 0; i < ARR_SIZE(patt) - desc->len; ++i) {
			ldst_val expected, read;

			expected.u64 = load_val[i];
			if (desc->len == 4) // float
				expected.i32[1] = -1;

			if (i % desc->len == 0)
				continue;

			printf("loadfp %u, insn %s, n=%u, off=%u, cmp=%" PRIx64 "\n",
				desc->len * 8, loadfp_funcs[j].name, desc->n, i, expected.u64);
			fflush(stdout);
			for (k = 0; k < desc->n; ++k) {
				read.f64 = load_f(&patt[i], j, k);
				if (read.u64 != expected.u64)
					error(1, 0, "failed at load_f %u %u %u: %" PRIx64,
						i, j, k, read.u64);
			}
		}
	}
	// Test int store
	for (j = 0; j < ARR_SIZE(store_funcs); ++j) {
		const mem_op_desc *desc = store_funcs[j].desc;

		for (i = 0; i < ARR_SIZE(patt) - desc->len; ++i) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverflow"
			ldst_val val = { 0x8899aabbccddeeffULL };
#pragma GCC diagnostic pop
			unsigned end = i + desc->len;

			if (desc->len != 1 && i % desc->len == 0)
				continue;

			memcpy(val.bytes, &patt[i], desc->len);
			printf("store %u, insn %s, n=%u, off=%u, src=%lx\n",
				desc->len * 8, store_funcs[j].name, desc->n, i, val.u_long);
			fflush(stdout);

			for (k = 0; k < desc->n; ++k) {
				uint8_t buff[ARR_SIZE(patt)] = {};

				memcpy(buff, patt, i);
				memcpy(&buff[end], &patt[end], ARR_SIZE(patt) - end);
				store_i(&buff[i], val.u_long, j, k);
				if (memcmp(buff, patt, sizeof(buff)))
					error(1, 0, "faild at store_i %u %u %u", i, j, k);
			}

		}
	}
	// Test fp store
	for (j = 0; j < ARR_SIZE(storefp_funcs); ++j) {
		const mem_op_desc *desc = storefp_funcs[j].desc;

		for (i = 0; i < ARR_SIZE(patt) - desc->len; ++i) {
			ldst_val val;
			unsigned end = i + desc->len;

			val.u32[0] = 0xccddeeffUL;
			val.u32[1] = 0x8899aabbUL;

			if (i % desc->len == 0)
				continue;

			memcpy(val.bytes, &patt[i], desc->len);
			printf("storefp %u, insn %s, n=%u, off=%u, src=%" PRIx64 "\n",
				desc->len * 8, storefp_funcs[j].name, desc->n, i, val.u64);
			fflush(stdout);

			for (k = 0; k < desc->n; ++k) {
				uint8_t buff[ARR_SIZE(patt)] = {};

				memcpy(buff, patt, i);
				memcpy(&buff[end], &patt[end], ARR_SIZE(patt) - end);
				store_f(&buff[i], val.f64, j, k);
				if (memcmp(buff, patt, sizeof(buff)))
					error(1, 0, "faild at store_f %u %u %u", i, j, k);
			}
		}
	}
	return 0;
}
