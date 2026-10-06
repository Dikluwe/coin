#include <Inventor/C/XML/attribute.h>
#include <Inventor/C/XML/document.h>
#include <Inventor/C/XML/element.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition, code) do {                                      \
  if (!(condition)) { fprintf(stderr, "DTD test failed: %d\n", code); \
    cc_xml_doc_delete_x(doc); return code; }                             \
} while (0)

static int requests;

static SbBool
resolve(void * userdata, const char * url, const char * publicid,
        const char ** content, size_t * bytes)
{
  static const char dtd[] =
    "<!ENTITY % declarations SYSTEM 'definitions.dtd'>"
    "%declarations;";
  static const char definitions[] =
    "<!ELEMENT root (#PCDATA)>"
    "<!ATTLIST root mode CDATA 'default'>"
    "<!ENTITY ext SYSTEM 'payload.txt'>";
  static const char payload[] = "from callback";
  (void)userdata;
  (void)publicid;
  ++requests;
  if (strcmp(url, "coin://test/external.dtd") == 0) {
    *content = dtd;
    *bytes = sizeof(dtd) - 1;
    return TRUE;
  }
  if (strcmp(url, "https://example.invalid/external.dtd") == 0) {
    *content = dtd;
    *bytes = sizeof(dtd) - 1;
    return TRUE;
  }
  if (strcmp(url, "coin://test/payload.txt") == 0) {
    *content = payload;
    *bytes = sizeof(payload) - 1;
    return TRUE;
  }
  if (strcmp(url, "coin://test/definitions.dtd") == 0 ||
      strcmp(url, "https://example.invalid/definitions.dtd") == 0) {
    *content = definitions;
    *bytes = sizeof(definitions) - 1;
    return TRUE;
  }
  return FALSE;
}

int
main(void)
{
  static const char internal[] =
    "<!DOCTYPE root [<!ELEMENT root (#PCDATA)>"
    "<!ATTLIST root id CDATA #REQUIRED>"
    "<!ENTITY greeting 'hello'>]>"
    "<root id='a'>&greeting;</root>";
  static const char invalid[] =
    "<!DOCTYPE root [<!ELEMENT root EMPTY>"
    "<!ATTLIST root id CDATA #REQUIRED>]>"
    "<root/>";
  static const char external[] =
    "<!DOCTYPE root SYSTEM 'external.dtd'><root>&ext;</root>";
  static const char denied[] =
    "<!DOCTYPE root SYSTEM 'file:///etc/passwd'><root/>";
  static const char remote[] =
    "<!DOCTYPE root SYSTEM 'https://example.invalid/external.dtd'>"
    "<root/>";
  cc_xml_doc * doc = cc_xml_doc_new();
  const cc_xml_elt * committed;
  const cc_xml_elt * root;
  const cc_xml_attr * attr;
  CHECK(doc != NULL, 1);

  CHECK(cc_xml_doc_read_buffer_dtd_x(doc, internal, sizeof(internal) - 1,
                                      "coin://test/", NULL, NULL), 2);
  root = cc_xml_doc_get_root(doc);
  CHECK(root != NULL && strcmp(cc_xml_elt_get_type(root), "root") == 0, 3);
  CHECK(cc_xml_elt_get_num_children(root) == 1, 4);
  CHECK(strcmp(cc_xml_elt_get_cdata(cc_xml_elt_get_child(root, 0)),
               "hello") == 0, 5);
  committed = root;

  CHECK(!cc_xml_doc_read_buffer_dtd_x(doc, invalid, sizeof(invalid) - 1,
                                       "coin://test/", NULL, NULL), 6);
  CHECK(cc_xml_doc_get_root(doc) == committed, 7);

  requests = 0;
  CHECK(cc_xml_doc_read_buffer_dtd_x(doc, external, sizeof(external) - 1,
                                      "coin://test/", resolve, NULL), 8);
  CHECK(requests == 3, 9);
  root = cc_xml_doc_get_root(doc);
  CHECK(cc_xml_elt_get_num_children(root) == 1, 10);
  CHECK(strcmp(cc_xml_elt_get_cdata(cc_xml_elt_get_child(root, 0)),
               "from callback") == 0, 11);
  attr = cc_xml_elt_get_attribute(root, "mode");
  CHECK(attr != NULL && strcmp(cc_xml_attr_get_value(attr), "default") == 0, 12);
  committed = root;

  requests = 0;
  CHECK(!cc_xml_doc_read_buffer_dtd_x(doc, denied, sizeof(denied) - 1,
                                       "coin://test/", resolve, NULL), 13);
  CHECK(requests == 1 && cc_xml_doc_get_root(doc) == committed, 14);

  requests = 0;
  CHECK(cc_xml_doc_read_buffer_dtd_x(doc, remote, sizeof(remote) - 1,
                                      "coin://test/", resolve, NULL), 15);
  CHECK(requests == 2, 16);

  cc_xml_doc_delete_x(doc);
  return 0;
}
