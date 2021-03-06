/**
 * @example dict.c
 *
 * This example shows how to use a dictionary.
 */

#include <stdio.h>
#include <inttypes.h>
#include <ingenialink/ingenialink.h>

static void print_scat(const char *id, il_dict_labels_t *labels)
{
	/* id */
	printf("\tID: %s\n", id);

	/* labels */
	printf("\tLabels:\n");

	if (labels && il_dict_labels_nlabels_get(labels) > 0) {
		size_t i;
		const char **langs;

		langs = il_dict_labels_langs_get(labels);

		for (i = 0; langs[i]; i++) {
			const char *label;

			(void)il_dict_labels_get(labels, langs[i], &label);

			printf("\t\t%s: %s\n", langs[i], label);
		}

		il_dict_labels_langs_destroy(langs);
	} else {
		printf("\t\tNone\n");
	}
}

static void print_cat(il_dict_t *dict, const char *id, il_dict_labels_t *labels)
{
	size_t i;
	const char **ids;

	/* id */
	printf("ID: %s\n", id);

	/* labels */
	printf("Labels:\n");

	if (labels && il_dict_labels_nlabels_get(labels) > 0) {
		size_t i;
		const char **langs;

		langs = il_dict_labels_langs_get(labels);

		for (i = 0; langs[i]; i++) {
			const char *label;

			(void)il_dict_labels_get(labels, langs[i], &label);

			printf("\t%s: %s\n", langs[i], label);
		}

		il_dict_labels_langs_destroy(langs);
	} else {
		printf("\tNone\n");
	}

	/* subcategories */
	printf("Sub-categories:\n");
	ids = il_dict_scat_ids_get(dict, id);
	if (!ids) {
		fprintf(stderr,
			"Could not obtain sub-categories: %s\n", ilerr_last());
		return;
	}

	for (i = 0; ids[i]; i++) {
		il_dict_labels_t *labels;

		(void)il_dict_scat_get(dict, id, ids[i], &labels);
		print_scat(ids[i], labels);
	}

	il_dict_scat_ids_destroy(ids);

	printf("==============================\n");
}

static void print_reg(const il_reg_t *reg)
{
	const char *name;
	const char **langs;
	size_t i;

	/* address */
	printf("Address: %08x\n", reg->address);

	/* data type */
	switch (reg->dtype) {
	case IL_REG_DTYPE_U8:
		name = "8-bit unsigned integer";
		break;
	case IL_REG_DTYPE_S8:
		name = "8-bit integer";
		break;
	case IL_REG_DTYPE_U16:
		name = "16-bit unsigned integer";
		break;
	case IL_REG_DTYPE_S16:
		name = "16-bit integer";
		break;
	case IL_REG_DTYPE_U32:
		name = "32-bit unsigned integer";
		break;
	case IL_REG_DTYPE_S32:
		name = "32-bit integer";
		break;
	case IL_REG_DTYPE_U64:
		name = "64-bit unsigned integer";
		break;
	case IL_REG_DTYPE_S64:
		name = "64-bit integer";
		break;
	default:
		name = "unknown";
	}

	printf("Data type: %s\n", name);

	/* access */
	switch (reg->access) {
	case IL_REG_ACCESS_RO:
		name = "read-only";
		break;
	case IL_REG_ACCESS_WO:
		name = "write-only";
		break;
	case IL_REG_ACCESS_RW:
		name = "read/write";
		break;
	default:
		name = "unknown";
	}

	printf("Access: %s\n", name);

	/* physical units */
	switch (reg->phy) {
	case IL_REG_PHY_NONE:
		name = "none";
		break;
	case IL_REG_PHY_TORQUE:
		name = "torque";
		break;
	case IL_REG_PHY_POS:
		name = "position";
		break;
	case IL_REG_PHY_VEL:
		name = "velocity";
		break;
	case IL_REG_PHY_ACC:
		name = "acceleration";
		break;
	case IL_REG_PHY_VOLT_REL:
		name = "relative voltage";
		break;
	case IL_REG_PHY_RAD:
		name = "radians";
		break;
	default:
		name = "unknown";
	}

	/* physical units */
	printf("Physical units: %s\n", name);

	/* range */
	printf("Range: ");

	switch (reg->dtype) {
	case IL_REG_DTYPE_U8:
		printf("(%"PRIu8", %"PRIu8")\n",
		       reg->range.min.u8, reg->range.max.u8);
		break;
	case IL_REG_DTYPE_S8:
		printf("(%"PRId8", %"PRId8")\n",
		       reg->range.min.s8, reg->range.max.s8);
		break;
	case IL_REG_DTYPE_U16:
		printf("(%"PRIu16", %"PRIu16")\n",
		       reg->range.min.u16, reg->range.max.u16);
		break;
	case IL_REG_DTYPE_S16:
		printf("(%"PRId16", %"PRId16")\n",
		       reg->range.min.s16, reg->range.max.s16);
		break;
	case IL_REG_DTYPE_U32:
		printf("(%"PRIu32", %"PRIu32")\n",
		       reg->range.min.u32, reg->range.max.u32);
		break;
	case IL_REG_DTYPE_S32:
		printf("(%"PRId32", %"PRId32")\n",
		       reg->range.min.s32, reg->range.max.s32);
		break;
	case IL_REG_DTYPE_U64:
		printf("(%"PRIu64", %"PRIu64")\n",
		       reg->range.min.u64, reg->range.max.u64);
		break;
	case IL_REG_DTYPE_S64:
		printf("(%"PRId64", %"PRId64")\n",
		       reg->range.min.s64, reg->range.max.s64);
		break;
	default:
		printf("Undefined\n");
	}

	/* labels */
	printf("Labels:\n");

	if (reg->labels && il_dict_labels_nlabels_get(reg->labels) > 0) {
		langs = il_dict_labels_langs_get(reg->labels);

		for (i = 0; langs[i]; i++) {
			const char *label;

			(void)il_dict_labels_get(reg->labels, langs[i], &label);

			printf("\t%s: %s\n", langs[i], label);
		}

		il_dict_labels_langs_destroy(langs);
	} else {
		printf("\tNone\n");
	}

	/* category and subcategory */
	printf("Category ID: %s\n", reg->cat_id);
	printf("Sub-category ID: %s\n", reg->scat_id);

	printf("==============================\n");
}

int main(int argc, const char **argv)
{
	int r = 0;
	il_dict_t *dict;
	const char **ids;
	size_t i;
	const il_reg_t *reg;

	if (argc < 2) {
		fprintf(stderr, "Usage: ./dict DICTIONARY.xml\n");
		return -1;
	}

	dict = il_dict_create(argv[1]);
	if (!dict) {
		fprintf(stderr, "Could not create dictionary: %s\n",
			ilerr_last());
		return -1;
	}

	/* show categories */
	ids = il_dict_cat_ids_get(dict);
	if (!ids) {
		fprintf(stderr,
			"Could not obtain categories: %s\n", ilerr_last());
		goto cleanup;
	}

	for (i = 0; ids[i]; i++) {
		il_dict_labels_t *labels;

		(void)il_dict_cat_get(dict, ids[i], &labels);
		print_cat(dict, ids[i], labels);
	}

	il_dict_cat_ids_destroy(ids);

	/* show registers */
	ids = il_dict_reg_ids_get(dict);
	if (!ids) {
		fprintf(stderr, "Could not obtain IDs: %s\n", ilerr_last());
		goto cleanup;
	}

	for (i = 0; ids[i]; i++) {
		(void)il_dict_reg_get(dict, ids[i], &reg);
		print_reg(reg);
	}

	il_dict_reg_ids_destroy(ids);

cleanup:
	il_dict_destroy(dict);

	return r;
}
