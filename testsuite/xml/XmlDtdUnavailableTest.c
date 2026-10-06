#include <Inventor/C/XML/document.h>
#include <Inventor/C/XML/element.h>

#include <string.h>

int
main(void)
{
  static const char dtd[] = "<!DOCTYPE root [<!ELEMENT root EMPTY>]><root/>";
  static const char ordinary[] = "<old/>";
  cc_xml_doc * doc = cc_xml_doc_new();
  const cc_xml_elt * oldroot;
  int success;
  if (doc == NULL) return 1;
  if (!cc_xml_doc_read_buffer_x(doc, ordinary, sizeof(ordinary) - 1)) {
    cc_xml_doc_delete_x(doc);
    return 2;
  }
  oldroot = cc_xml_doc_get_root(doc);
  success = cc_xml_doc_read_buffer_dtd_x(doc, dtd, sizeof(dtd) - 1,
                                         NULL, NULL, NULL);
  if (success || cc_xml_doc_get_root(doc) != oldroot ||
      strcmp(cc_xml_elt_get_type(oldroot), "old") != 0) {
    cc_xml_doc_delete_x(doc);
    return 3;
  }
  cc_xml_doc_delete_x(doc);
  return 0;
}
