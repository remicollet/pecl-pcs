/*
  +----------------------------------------------------------------------+
  | PCS test extension <http://ptest.tekwire.net>                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 2015 The PHP Group                                     |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt.                                 |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Francois Laupretre <francois@tekwire.net>                    |
  +----------------------------------------------------------------------+
*/
/* This extension is used to test the PCS extension */

/*============================================================================*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"

/*============================================================================*/

#include "../../pcs_client/client.h"

/*============================================================================*/

#include "php_ptest.h"

#if PHP_MAJOR_VERSION >= 7
#	define PHP_7
#endif

/*------------------------*/

#ifdef COMPILE_DL_PTEST
#	ifdef PHP_7
#		ifdef ZTS
			ZEND_TSRMLS_CACHE_DEFINE();
#		endif
#	endif
	ZEND_GET_MODULE(ptest)
#endif

/*------------------------*/

ZEND_BEGIN_MODULE_GLOBALS(ptest)

zend_long test_case;
zend_bool load_code1;
zend_bool load_code2;
zend_bool load_code3;
zend_bool load_empty;

ZEND_END_MODULE_GLOBALS(ptest)

ZEND_DECLARE_MODULE_GLOBALS(ptest)

#ifdef ZTS
#	ifdef PHP_7
#		define PTEST_G(v) ZEND_TSRMG(ptest_globals_id, zend_ptest_globals *, v)
#	else
#		define PTEST_G(v) TSRMG(ptest_globals_id, zend_ptest_globals *, v)
#	endif
#else
#	define PTEST_G(v) (ptest_globals.v)
#endif

zend_long pcs_file_count;

/*============================================================================*/

#include "php/php.c/code1.php.c"

#include "php/php.c/code2.php.c"

#include "php/php.c/code3.php.c"

#include "php/php.c/empty.php.c"

/*============================================================================*/
/*---------------------------------------------------------------*/
/* Dummy function called from PHP */

PHP_FUNCTION(ptest_add)
{
	zend_long a, b;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "ll", &a, &b) == FAILURE) {
		return;
	}
	
	RETURN_LONG(a + b);
}

/*---------------------------------------------------------------*/
/* Dummy function calling PHP from C */

PHP_FUNCTION(ptest_c_to_php_test)
{
	zval func, arg;
	zend_string *msg;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "S", &msg) == FAILURE) {
		return;
	}
	
	ZVAL_STRING(&func, "PCS_Test\\Dummy5::hello");
	ZVAL_STR(&arg, msg);
	call_user_function(NULL, NULL, &func, return_value, 1, &arg);
	zval_ptr_dtor(&func);
}

/*============================================================================*/
/*---------------------------------------------------------------*/
/* phpinfo() output                                              */

static PHP_MINFO_FUNCTION(ptest)
{
	char buf[10];

	php_info_print_table_start();

	php_info_print_table_row(2, "PCS test extension", "enabled");
	php_info_print_table_row(2, "Version", PHP_PTEST_VERSION);

	/* Let's display the number of PHP scripts we registered in PCS */

	sprintf(buf,"%d", (int)pcs_file_count);
	php_info_print_table_row(2, "Scripts registered in PCS", buf);

	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

/*---------------------------------------------------------------*/
/* Initialize a new zend_ptest_globals struct during thread spin-up */

static void ptest_globals_ctor(zend_ptest_globals * globals TSRMLS_DC)
{
#if defined(PHP_7) && defined(COMPILE_DL_PTEST) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	memset(globals, 0, sizeof(*globals)); /* Init everything to 0/NULL */
}

/*------------------------*/
/* Any resources allocated during initialization may be freed here */

#ifndef ZTS
static void ptest_globals_dtor(zend_ptest_globals * globals TSRMLS_DC)
{
}
#endif

/*---------------------------------------------------------------*/
/* Ini settings */

PHP_INI_BEGIN()

STD_PHP_INI_ENTRY("ptest.test_case", "0", PHP_INI_ALL, OnUpdateLong, test_case,
	zend_ptest_globals, ptest_globals)
STD_PHP_INI_BOOLEAN("ptest.load_code1", "0", PHP_INI_ALL, OnUpdateBool, load_code1,
	zend_ptest_globals, ptest_globals)
STD_PHP_INI_BOOLEAN("ptest.load_code2", "0", PHP_INI_ALL, OnUpdateBool, load_code2,
	zend_ptest_globals, ptest_globals)
STD_PHP_INI_BOOLEAN("ptest.load_code3", "0", PHP_INI_ALL, OnUpdateBool, load_code3,
	zend_ptest_globals, ptest_globals)
STD_PHP_INI_BOOLEAN("ptest.load_empty", "0", PHP_INI_ALL, OnUpdateBool, load_empty,
	zend_ptest_globals, ptest_globals)

PHP_INI_END()

/*---------------------------------------------------------------*/

static PHP_RINIT_FUNCTION(ptest)
{
	return SUCCESS;
}

/*---------------------------------------------------------------*/

static PHP_RSHUTDOWN_FUNCTION(ptest)
{
	return SUCCESS;
}

/*---------------------------------------------------------------*/

static PHP_MINIT_FUNCTION(ptest)
{
	zend_long count;

	ZEND_INIT_MODULE_GLOBALS(ptest, ptest_globals_ctor, NULL);
	REGISTER_INI_ENTRIES();

	pcs_file_count = 0;

	if (PTEST_G(load_code1)) {
		if ((count = PCS_registerDescriptors(code1, 0)) == FAILURE) return FAILURE;
		php_printf("Loaded code1 set\n");
		pcs_file_count += count;
	}

	if (PTEST_G(load_code2)) {
		if ((count = PCS_registerDescriptors(code2, 0)) == FAILURE) return FAILURE;
		php_printf("Loaded code2 set\n");
		pcs_file_count += count;
	}

	if (PTEST_G(load_code3)) {
		if ((count = PCS_registerDescriptors(code3, 0)) == FAILURE) return FAILURE;
		php_printf("Loaded code3 set\n");
		pcs_file_count += count;
	}

	if (PTEST_G(load_empty)) {
		if ((count = PCS_registerDescriptors(empty, 0)) == FAILURE) return FAILURE;
		php_printf("Loaded empty set\n");
		pcs_file_count += count;
	}
	
	return SUCCESS;
}

/*---------------------------------------------------------------*/

static PHP_MSHUTDOWN_FUNCTION(ptest)
{
	UNREGISTER_INI_ENTRIES();

#ifndef ZTS
		ptest_globals_dtor(&ptest_globals TSRMLS_CC);
#endif

	return SUCCESS;
}

/*---------------------------------------------------------------*/
/*-- Functions --*/

ZEND_BEGIN_ARG_INFO_EX(UT_1arg_arginfo, 0, 0, 1)
ZEND_ARG_INFO(0, arg1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(UT_2args_arginfo, 0, 0, 2)
ZEND_ARG_INFO(0, arg1)
ZEND_ARG_INFO(0, arg2)
ZEND_END_ARG_INFO()

static zend_function_entry ptest_functions[] = {
	PHP_FE(ptest_c_to_php_test, UT_1arg_arginfo)
	PHP_FE(ptest_add, UT_2args_arginfo)
    PHP_FE_END  /* must be the last line */
};

/*---------------------------------------------------------------*/
/*-- Module definition --*/

static const zend_module_dep ptest_deps[] = {
	ZEND_MOD_REQUIRED("pcs")
	ZEND_MOD_END
};
	
zend_module_entry ptest_module_entry = {
	STANDARD_MODULE_HEADER_EX,
	NULL,
	ptest_deps,
/*============================================================================*/
	PHP_PTEST_EXTNAME,
	ptest_functions,
	PHP_MINIT(ptest),
	PHP_MSHUTDOWN(ptest),
	PHP_RINIT(ptest),
	PHP_RSHUTDOWN(ptest),
	PHP_MINFO(ptest),
	PHP_PTEST_VERSION,
	STANDARD_MODULE_PROPERTIES
};

/*---------------------------------------------------------------*/