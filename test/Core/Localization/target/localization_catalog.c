#include "unity.h"
#include <string.h>
#include "LocalizationCatalog.generated.c"
#include "../../../../src/Core/Localization/Localization.c"

/* Real generated catalogs and the production selector; no localization stubs. */
void target_localization_verify_catalogs(void) {
    const LocalizationCatalog *original = Localization_Snapshot();
    TEST_ASSERT_NOT_NULL(original);
    TEST_ASSERT_TRUE(Localization_Count() >= 2);
    const LocalizationCatalog *english = Localization_Find("en");
    const LocalizationCatalog *turkish = Localization_Find("tr");
    TEST_ASSERT_NOT_NULL(english);
    TEST_ASSERT_NOT_NULL(turkish);
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    TEST_ASSERT_EQUAL_PTR(english, Localization_Snapshot());
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL_PTR(turkish, Localization_Snapshot());
    TEST_ASSERT_EQUAL_STRING("en", english->code);
    TEST_ASSERT_FALSE(Localization_Apply("missing"));
    TEST_ASSERT_EQUAL_PTR(turkish, Localization_Snapshot());
    TEST_ASSERT_TRUE(Localization_Apply(original->code));
    TEST_ASSERT_EQUAL_PTR(original, Localization_Snapshot());
}
