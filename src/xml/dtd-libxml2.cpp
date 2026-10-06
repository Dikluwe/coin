/**************************************************************************\
 * Copyright (c) Kongsberg Oil & Gas Technologies AS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\**************************************************************************/

#include <Inventor/C/XML/document.h>

#ifdef COIN_XML_DTD_LIBXML2

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>

#ifndef LIBXML_VALID_ENABLED
#error "Coin DTD validation requires libxml2 built with DTD validation"
#endif

namespace {

struct CoinXmlDtdResolver {
  cc_xml_resource_resolver_cb * callback;
  void * userdata;
};

xmlParserErrors
coin_xml_dtd_resource_loader(void * context, const char * url,
                             const char * publicid, xmlResourceType type,
                             xmlParserInputFlags, xmlParserInput ** output)
{
  CoinXmlDtdResolver * resolver = static_cast<CoinXmlDtdResolver *>(context);
  *output = NULL;
  if (type != XML_RESOURCE_DTD && type != XML_RESOURCE_GENERAL_ENTITY &&
      type != XML_RESOURCE_PARAMETER_ENTITY) return XML_IO_LOAD_ERROR;
  if (resolver->callback == NULL) return XML_IO_LOAD_ERROR;

  const char * content = NULL;
  size_t bytes = 0;
  if (!resolver->callback(resolver->userdata, url, publicid,
                          &content, &bytes)) return XML_IO_LOAD_ERROR;
  if (content == NULL && bytes != 0) return XML_IO_LOAD_ERROR;

  // With flags 0 libxml2 copies the callback's bytes into its own input.
  static const char empty[] = "";
  *output = xmlNewInputFromMemory(url, content ? content : empty, bytes,
                                  static_cast<xmlParserInputFlags>(0));
  return *output ? XML_ERR_OK : XML_ERR_NO_MEMORY;
}

} // namespace

#endif // COIN_XML_DTD_LIBXML2

SbBool
cc_xml_doc_read_buffer_dtd_x(cc_xml_doc * doc, const char * buffer,
                             size_t buflen, const char * base_url,
                             cc_xml_resource_resolver_cb * resolver,
                             void * userdata)
{
#ifdef COIN_XML_DTD_LIBXML2
  if (doc == NULL || buffer == NULL || buflen > INT_MAX) return FALSE;

  xmlParserCtxt * context = xmlNewParserCtxt();
  if (context == NULL) return FALSE;

  CoinXmlDtdResolver resource = { resolver, userdata };
  xmlCtxtSetResourceLoader(context, coin_xml_dtd_resource_loader, &resource);
  xmlDoc * parsed = xmlCtxtReadMemory(context, buffer,
                                      static_cast<int>(buflen),
                                      base_url ? base_url : "coin://memory/",
                                      NULL,
                                      XML_PARSE_DTDVALID | XML_PARSE_DTDATTR |
                                      XML_PARSE_NOENT | XML_PARSE_NONET |
                                      XML_PARSE_NO_SYS_CATALOG);
  const xmlParserStatus status = xmlCtxtGetStatus(context);
  xmlFreeParserCtxt(context);

  if (parsed == NULL || status != 0 ||
      (parsed->intSubset == NULL && parsed->extSubset == NULL)) {
    if (parsed != NULL) xmlFreeDoc(parsed);
    return FALSE;
  }

  // All entities were expanded and the document validated. Do not pass a
  // second DOCTYPE through Expat: it has no resolver or DTD validator here.
  if (parsed->intSubset != NULL) {
    xmlDtd * dtd = parsed->intSubset;
    xmlUnlinkNode(reinterpret_cast<xmlNode *>(dtd));
    xmlFreeDtd(dtd);
  }

  xmlChar * normalized = NULL;
  int normalizedbytes = 0;
  xmlDocDumpMemoryEnc(parsed, &normalized, &normalizedbytes, "UTF-8");
  xmlFreeDoc(parsed);
  if (normalized == NULL || normalizedbytes <= 0) {
    if (normalized != NULL) xmlFree(normalized);
    return FALSE;
  }

  const SbBool success = cc_xml_doc_read_buffer_x(
    doc, reinterpret_cast<const char *>(normalized),
    static_cast<size_t>(normalizedbytes));
  xmlFree(normalized);
  return success;
#else
  (void)doc;
  (void)buffer;
  (void)buflen;
  (void)base_url;
  (void)resolver;
  (void)userdata;
  return FALSE;
#endif
}
