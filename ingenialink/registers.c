/*
 * MIT License
 *
 * Copyright (c) 2017 Ingenia-CAT S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "registers.h"

#include <string.h>

#include "ingenialink/err.h"

/*******************************************************************************
 * Public
 ******************************************************************************/

il_reg_labels_t *il_reg_labels_create()
{
	il_reg_labels_t *labels;

	labels = malloc(sizeof(*labels));
	if (!labels) {
		ilerr__set("Labels dictionary allocation failed");
		return NULL;
	}

	/* create hash table for labels */
	labels->h = kh_init(str);
	if (!labels->h) {
		ilerr__set("Labels hash table allocation failed");
		goto cleanup_labels;
	}

	return labels;

cleanup_labels:
	free(labels);
	return NULL;
}

void il_reg_labels_destroy(il_reg_labels_t *labels)
{
	khint_t k;

	for (k = 0; k < kh_end(labels->h); ++k) {
		if (kh_exist(labels->h, k)) {
			free((char *)kh_key(labels->h, k));
			free((char *)kh_val(labels->h, k));
		}
	}

	kh_destroy(str, labels->h);

	free(labels);
}

int il_reg_labels_get(il_reg_labels_t *labels, const char *lang,
		      const char **label)
{
	khint_t k;

	k = kh_get(str, labels->h, lang);
	if (k == kh_end(labels->h)) {
		ilerr__set("Language not available (%s)", lang);
		return IL_EFAIL;
	}

	*label = kh_value(labels->h, k);

	return 0;
}

void il_reg_labels_set(il_reg_labels_t *labels, const char *lang,
		       const char *label)
{
	int absent;
	khint_t k;

	k = kh_put(str, labels->h, lang, &absent);
	if (absent)
		kh_key(labels->h, k) = strdup(lang);
	else
		free((char *)kh_val(labels->h, k));

	kh_val(labels->h, k) = strdup(label);
}

void il_reg_labels_del(il_reg_labels_t *labels, const char *lang)
{
	khint_t k;

	k = kh_get(str, labels->h, lang);
	if (k != kh_end(labels->h)) {
		free((char *)kh_key(labels->h, k));
		free((char *)kh_val(labels->h, k));

		kh_del(str, labels->h, k);
	}
}

size_t il_reg_labels_nlabels_get(il_reg_labels_t *labels)
{
	return (size_t)kh_size(labels->h);
}

const char **il_reg_labels_langs_get(il_reg_labels_t *labels)
{
	const char **langs;
	size_t i;
	khint_t k;

	/* allocate array for register keys */
	langs = malloc(sizeof(char *) *
		       (il_reg_labels_nlabels_get(labels) + 1));
	if (!langs) {
		ilerr__set("Languages array allocation failed");
		return NULL;
	}

	/* assign keys, null-terminate */
	for (i = 0, k = 0; k < kh_end(labels->h); ++k) {
		if (kh_exist(labels->h, k)) {
			langs[i] = (const char *)kh_key(labels->h, k);
			i++;
		}
	}

	langs[i] = NULL;

	return langs;
}

void il_reg_labels_langs_destroy(const char **langs)
{
	free((char **)langs);
}

