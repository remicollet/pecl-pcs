/*
  +----------------------------------------------------------------------+
  | PCS extension <http://PCS.tekwire.net>						 |
  +----------------------------------------------------------------------+
  | Copyright (c) 2015 The PHP Group									 |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,		 |
  | that is bundled with this package in the file LICENSE, and is		 |
  | available through the world-wide-web at the following url:			 |
  | http://www.php.net/license/3_01.txt.								 |
  | If you did not receive a copy of the PHP license and are unable to	 |
  | obtain it through the world-wide-web, please send a note to			 |
  | license@php.net so we can mail you a copy immediately.				 |
  +----------------------------------------------------------------------+
  | Author: Francois Laupretre <francois@tekwire.net>					 |
  +----------------------------------------------------------------------+
*/

#include "Zend/zend_API.h"
#include "Zend/zend_compile.h"
#include "Zend/zend_execute.h"
#include "Zend/zend_hash.h"

/*==========================================================================*/

/*---------------------------------------------------------------*/
/* Register the PCS autoloader */

static void PCS_Loader_registerHook(TSRMLS_D)
{
	zval ret;

#ifdef PHP_7
	zval func, args[3];

	ZVAL_STR(&func, spl_ar_func_name);
	ZVAL_STR(&(args[0]), hook_func_name);
	ZVAL_TRUE(&(args[1]));	/* do_throw */
	ZVAL_TRUE(&(args[2]));	/* prepend */

	call_user_function(NULL, NULL, &func, &ret, 3, args TSRMLS_CC);
#else
	zval *func_zp, *args[3];

	MAKE_STD_ZVAL(func_zp);
	ZVAL_STRINGL(func_zp, ZSTR_VAL(spl_ar_func_name), ZSTR_LEN(spl_ar_func_name), 1);

	MAKE_STD_ZVAL(args[0]);
	ZVAL_STRINGL(args[0], ZSTR_VAL(hook_func_name), ZSTR_LEN(hook_func_name), 1);
	MAKE_STD_ZVAL(args[1]);
	ZVAL_TRUE(args[1]);
	MAKE_STD_ZVAL(args[2]);
	ZVAL_TRUE(args[2]);

	call_user_function(NULL, NULL, func_zp, &ret, 3,	args TSRMLS_CC);

	zval_ptr_dtor(&func_zp);
	zval_ptr_dtor(&(args[0]));
	zval_ptr_dtor(&(args[1]));
	zval_ptr_dtor(&(args[2]));
#endif

	compat_zval_ptr_dtor(&ret);

}

/*---------------------------------------------------------------*/
/* {{{ proto void \PCS\Mgr::autoloadHook(string symbol [, string type ]) */

static PHP_METHOD(PCS, autoloadHook)
{
	char *symbol,*type, ctype;
	PCS_SIZE_T slen,tlen;

	type=NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS()TSRMLS_CC, "s|s",&symbol, &slen
		,&type,&tlen)==FAILURE) EXCEPTION_ABORT("Cannot parse parameters");

	ctype = (type ? (*type) : PCS_T_CLASS);
	DBG_MSG2("-> PCS autoloader(%s %s)", PCS_Loader_keyTypeString(ctype), symbol);

	PCS_Loader_loadSymbol(ctype , symbol, slen, 1, 0 TSRMLS_CC);

	DBG_MSG("<- PCS autoloader");
}

/*---------------------------------------------------------------*/
/* Returns SUCCESS/FAILURE */

static int PCS_Loader_loadSymbol(char type, char *symbol, PCS_SIZE_T slen, zend_bool autoload
	, zend_bool exception TSRMLS_DC)
{
	zend_string *key = NULL;
	PCS_Node *node;

	DBG_MSG2("-> PCS_Loader_loadSymbol(%c, %s)", type, symbol);

	/* If executed from the autoloader, no need to check for symbol existence */
	if ((!autoload) && PCS_Loader_symbolIsDefined(type, symbol, slen TSRMLS_CC)) {
		return SUCCESS;
	}

	key = zend_string_alloc(slen + 1, 0);
	ZSTR_VAL(key)[0] = type;
	memcpy(ZSTR_VAL(key) + 1, symbol, slen);
	ZSTR_VAL(key)[slen + 1] = '\0';

	node = zend_hash_find_ptr(symbols, key);
	zend_string_release(key);
	if (! node) {
		if ((exception)&&(!EG(exception))) {
			THROW_EXCEPTION_2("PCS: Unknown %s (%s)"
				, PCS_Loader_keyTypeString(type), symbol);
		}
		return FAILURE;
	}

	if (FAILURE == PCS_Loader_loadNode(node, exception TSRMLS_CC)) {
		return FAILURE;
	}

	return SUCCESS;
}

/*---------------------------------------------------------------*/
/* Return 0|1 */

static int PCS_Loader_symbolIsDefined(char type, char *symbol, PCS_SIZE_T slen TSRMLS_DC)
{
	char *lc_symbol = NULL;
	int status;

	if (!slen) return 0;

	if (type != PCS_T_CONSTANT) {
		lc_symbol = zend_str_tolower_dup(symbol, slen);
	}

	status=0;
	switch(type) {
		case PCS_T_CONSTANT:
			status = compat_zend_hash_str_exists(EG(zend_constants), symbol, slen);
			break;

		case PCS_T_FUNCTION:
			status = compat_zend_hash_str_exists(EG(function_table), lc_symbol, slen);
			break;

		case PCS_T_CLASS: /* Also works for interfaces and traits */
			status = compat_zend_hash_str_exists(EG(class_table), lc_symbol, slen);
			break;

		EMPTY_SWITCH_DEFAULT_CASE()
		}

	if (lc_symbol) {
		EFREE(lc_symbol);
	}

	return status;
}

/*--------------------*/
/* Execute a PHP script
   Adapted from zend_execute_scripts()
   We use zend_compile_file() and not zend_compile_string() because, in the
   future, opcache will cache PCS URIs.
   Returns SUCCESS|FAILURE
   if throw arg is set, throws exception on error
*/

static int PCS_Loader_loadNode(PCS_Node *node, int throw TSRMLS_DC)
{
	zend_file_handle file_handle;

	ZEND_ASSERT(node);
	DBG_MSG1("-> PCS_Loader_loadNode(%s)",ZSTR_VAL(node->path));

	if (!PCS_NODE_IS_FILE(node)) {
		if (throw) {
			THROW_EXCEPTION_1("%s: node is not a regular file - load aborted"
				, ZSTR_VAL(node->uri));
		}
		return FAILURE;
	}
	file_handle.type = ZEND_HANDLE_FILENAME;
	file_handle.handle.fd = 0;
	file_handle.handle.fp = NULL;
	file_handle.filename = ZSTR_VAL(node->uri);
	file_handle.opened_path = NULL;
	file_handle.free_filename = 0;

	{
#ifdef PHP_7
	zend_op_array *op_array;
	zval zret;

	op_array = zend_compile_file(&file_handle, ZEND_REQUIRE TSRMLS_CC);
	zend_destroy_file_handle(&file_handle TSRMLS_CC);
	if (!op_array) {
		if (throw) {
			THROW_EXCEPTION_1("%s: Error compiling script - load aborted"
				, ZSTR_VAL(node->uri));
		}
		return FAILURE;
	}

	EG(no_extensions)=1;
	zend_try {
		ZVAL_UNDEF(&zret);
		zend_execute(op_array, &zret);
	} zend_catch {
		destroy_op_array(op_array TSRMLS_CC);
		EFREE(op_array);
		zend_bailout();
	} zend_end_try();
	EG(no_extensions)=0;

	zval_ptr_dtor(&zret);
	destroy_op_array(op_array TSRMLS_CC);
	EFREE(op_array);
#else
	zend_op_array *orig_op_array = EG(active_op_array);
	zval **orig_retval_ptr_ptr = EG(return_value_ptr_ptr);

	EG(active_op_array) = zend_compile_file(&file_handle, ZEND_REQUIRE TSRMLS_CC);
	zend_destroy_file_handle(&file_handle TSRMLS_CC);
	if (! EG(active_op_array)) {
		if (throw) {
			THROW_EXCEPTION_1("%s: Error compiling script - load aborted"
				, ZSTR_VAL(node->uri));
		}
		return FAILURE;
	}

	EG(return_value_ptr_ptr) = NULL;
	zend_execute(EG(active_op_array) TSRMLS_CC);
	destroy_op_array(EG(active_op_array) TSRMLS_CC);
	efree(EG(active_op_array));

	EG(active_op_array) = orig_op_array;
	EG(return_value_ptr_ptr) = orig_retval_ptr_ptr;
#endif
	}

	return SUCCESS;
}

/*--------------------*/

static char *PCS_Loader_keyTypeString(char c)
{
	switch(c) {
		case 'L':
			return "class";
			break;
		case 'F':
			return "function";
			break;
		case 'C':
			return "constant";
			break;
		default:
			return "unknown";
	}
}

/*---------------------------------------------------------------*/

#define PCS_GET_FUNCTION(_name, _type) \
	PCS_GET_REQUIRE_FUNCTION(_name, _type, get, 0)

#define PCS_REQUIRE_FUNCTION(_name, _type) \
	PCS_GET_REQUIRE_FUNCTION(_name, _type, require, 1)

#define PCS_GET_REQUIRE_FUNCTION(_name, _type, _gtype, _exception) \
	static PHP_METHOD(PCS, _gtype ## _name) \
	{ \
		char *symbol; \
		PCS_SIZE_T slen; \
		zend_bool autoload = 0; \
 \
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|b", &symbol \
			, &slen, &autoload) == FAILURE) { \
			EXCEPTION_ABORT("Cannot parse parameters"); \
		} \
 \
		RETURN_BOOL(PCS_Loader_loadSymbol(_type, symbol, slen \
			, autoload, _exception TSRMLS_CC) == SUCCESS); \
	}

PCS_GET_FUNCTION(Function,PCS_T_FUNCTION)
PCS_GET_FUNCTION(Constant,PCS_T_CONSTANT)
PCS_GET_FUNCTION(Class,PCS_T_CLASS)

PCS_REQUIRE_FUNCTION(Function,PCS_T_FUNCTION)
PCS_REQUIRE_FUNCTION(Constant,PCS_T_CONSTANT)
PCS_REQUIRE_FUNCTION(Class,PCS_T_CLASS)

/*==========================================================================*/
/* Symbol registration */

/*---------------------------------------------------------------*/

static int PCS_Loader_registerNode(PCS_Node *node TSRMLS_DC)
{
	int do_parse, status;
	char *suf;
	zval ret, *zkey;
	zend_string *zp;
	HashTable *ht;

	ZEND_ASSERT(PCS_NODE_IS_FILE(node));
	DBG_MSG1("-> PCS_Loader_registerNode(%s)",ZSTR_VAL(node->path));

	/* Should we parse this script ? */

	switch(node->flags & PCS_AUTOLOAD_MASK) {
		case PCS_AUTOLOAD_FORCE:
			do_parse = 1;
			break;

		case PCS_AUTOLOAD_DISABLE:
			do_parse = 0;
			break;

		default:
			suf = &(ZSTR_VAL(node->path)[ZSTR_LEN(node->path) - 4]);
			do_parse = ((ZSTR_LEN(node->path) > 4)
				&&	(suf[0] == '.')
				&& ((suf[1] == 'p') || (suf[1] == 'P'))
				&& ((suf[2] == 'h') || (suf[2] == 'H'))
				&& ((suf[3] == 'p') || (suf[3] == 'P')));
	}

	if (! do_parse) return SUCCESS;

	/* Execute parser on script */

	DBG_MSG1("Parsing script %s", ZSTR_VAL(node->path));

	{
#ifdef PHP_7
	zend_string *data;
	zval zdata, func;

	data = zend_string_init(PCS_FILE_DATA(node), PCS_FILE_LEN(node), 0);
	ZVAL_STR(&zdata, data);
	ZVAL_STR(&func, parser_func_name);
	status = call_user_function(NULL, NULL, &func, &ret, 1, &zdata TSRMLS_CC);

	zend_string_release(data);
#else
	zval *func_zp, *arg;

	MAKE_STD_ZVAL(func_zp);
	ZVAL_STRINGL(func_zp, ZSTR_VAL(parser_func_name), ZSTR_LEN(parser_func_name), 1);

	MAKE_STD_ZVAL(arg);
	ZVAL_STRINGL(arg, PCS_FILE_DATA(node), PCS_FILE_LEN(node), 1);

	status=call_user_function(NULL, NULL, func_zp, &ret, 1,	&arg TSRMLS_CC);

	zval_ptr_dtor(&func_zp);
	zval_ptr_dtor(&arg);
#endif
	}

	if (status == FAILURE) {
		compat_zval_ptr_dtor(&ret);
		return FAILURE;
	}

	if (Z_TYPE(ret) != IS_ARRAY) {
		compat_zval_ptr_dtor(&ret);
		php_error(E_CORE_ERROR, "%s: Parser result should be an array"
			, ZSTR_VAL(node->uri));
		return FAILURE;
	}

	/* Register every symbols returned by the parser */

	ht = Z_ARRVAL(ret);
	for (zend_hash_internal_pointer_reset(ht);;zend_hash_move_forward(ht)) {
		if (zend_hash_has_more_elements(ht) != SUCCESS) break;
		zkey = compat_zend_hash_get_current_zval(ht);
		if (Z_TYPE_P(zkey) != IS_STRING) {
			compat_zval_ptr_dtor(&ret);
			php_error(E_CORE_ERROR, "%s: Elements returned by the parser should be strings"
				, ZSTR_VAL(node->uri));
			return FAILURE;
		}
		/* Register a persistent copy of the returned symbol */
#ifdef PHP_7
		zp = zend_string_dup(Z_STR_P(zkey), 1);
#else
		zp = zend_string_init(Z_STRVAL_P(zkey), Z_STRLEN_P(zkey), 1);
#endif
		status = PCS_Loader_registerKey(zp, node);
		if (status == FAILURE) {
			compat_zval_ptr_dtor(&ret);
			return FAILURE;
		}
	}

	compat_zval_ptr_dtor(&ret);
	return SUCCESS;
}

/*--------------------*/

static int PCS_Loader_registerKey(zend_string *key, PCS_Node *node)
{
	PCS_Node *oldnode;

	ZEND_ASSERT(key);
	ZEND_ASSERT(zend_string_is_persistent(key)); /* Input key must be persistent */
	ZEND_ASSERT(node);

	oldnode = zend_hash_find_ptr(symbols, key);
	if (oldnode) {
		php_error(E_CORE_ERROR, "%s %s: Symbol already defined by %s"
			, ZSTR_VAL(key) + 1
			, PCS_Loader_keyTypeString(ZSTR_VAL(key)[0])
			, ZSTR_VAL(oldnode->path));
		return FAILURE;
	}

	if (! zend_hash_add_new_ptr(symbols, key, node)) {
		php_error(E_CORE_ERROR, "%s %s: Cannot add symbol to symbol table"
			, ZSTR_VAL(key) + 1
			, PCS_Loader_keyTypeString(ZSTR_VAL(key)[0]));
		return FAILURE;
	}
	DBG_MSG1("%s: Registered key in PCS autoloader", ZSTR_VAL(key));

	return SUCCESS;
}

/*--------------------*/
/* Executed at MINIT
   Creates the symbol table and registers the parser
   This code must not throw exceptions
*/

static int PCS_Loader_moduleInit()
{
	PCS_Node *node;

	/* Init symbol table */
	/* No destructor because PCS_Node structs are destroyed with the tree */

	MutexSetup(symbols);
	symbols = ut_pallocate(NULL, sizeof(*symbols));
	zend_hash_init(symbols, 32, 0, NULL, 1);

	/* Register the parser */

	if (PCS_registerDescriptors(parser_code, PCS_AUTOLOAD_DISABLE) == FAILURE) {
		return FAILURE;
	}

	node = PCS_Tree_getNodeFromPath(IMM_STRL("internal/Parser/ParserInterface.php"));
	if (!node) {
		php_error(E_CORE_ERROR, "Registering PCS parser - Cannot get node (internal/Parser/ParserInterface.php)");
		return FAILURE;
	}
	ParserInterface_node = node;

	node = PCS_Tree_getNodeFromPath(IMM_STRL("internal/Parser/StringParser.php"));
	if (!node) {
		php_error(E_CORE_ERROR, "Registering PCS parser - Cannot get node (internal/Parser/StringParser.php)");
		return FAILURE;
	}
	StringParser_node = node;

	return SUCCESS;
}

/*--------------------*/
/* Register symbols from the file tree during RINIT
   This cannot be done during MINIT because the parser is written in PHP
   On entry, the parser is already registered
*/

static int PCS_Loader_Init(TSRMLS_D)
{
	PCS_Node *node;

	/* Load parser */

	PCS_Loader_loadNode(ParserInterface_node, 1 TSRMLS_CC);
	ON_EXCEPTION_RETURN(FAILURE);
	PCS_Loader_loadNode(StringParser_node, 1 TSRMLS_CC);
	ON_EXCEPTION_RETURN(FAILURE);

	ZEND_HASH_FOREACH_PTR(fileList, node) {
		PCS_Loader_registerNode(node TSRMLS_CC);
	} ZEND_HASH_FOREACH_END();

	loader_init_done = 1;
	return SUCCESS;
}

/*===============================================================*/

static zend_always_inline int MINIT_PCS_Loader(TSRMLS_D)
{
	/*--- Check that SPL is present */

	if (!ut_extension_loaded(IMM_STRL("spl") TSRMLS_CC)) {
		THROW_EXCEPTION("PCS requires the SPL extension");
		return FAILURE;
	}

	/* Init constant zvals */

	spl_ar_func_name = zend_string_init(IMM_STRL("spl_autoload_register"), 1);
	hook_func_name = zend_string_init(IMM_STRL("PCS\\Mgr::autoloadHook"), 1);
	parser_func_name = zend_string_init(IMM_STRL("PCS\\Parser\\StringParser::parse"), 1);

	/* Create and init symbol table */

	return PCS_Loader_moduleInit();
}

/*---------------------------------------------------------------*/

static zend_always_inline int MSHUTDOWN_PCS_Loader(TSRMLS_D)
{
	/* Destroy constant zvals */

	zend_string_release(spl_ar_func_name);
	zend_string_release(hook_func_name);
	zend_string_release(parser_func_name);

	/* Free symbol table */

	zend_hash_destroy(symbols);
	PFREE(symbols);
	MutexShutdown(symbols);

	return SUCCESS;
}

/*---------------------------------------------------------------*/

static zend_always_inline int RINIT_PCS_Loader(TSRMLS_D)
{
	int status;

	PCS_Loader_registerHook(TSRMLS_C);

	/* Mutex ensures symbol table is populated only once */

	MutexLock(symbols);
	if (! loader_init_done) {
		status = PCS_Loader_Init(TSRMLS_C);
		}
	MutexUnlock(symbols);
	if (status == FAILURE) return FAILURE;

	return SUCCESS;
}
/*---------------------------------------------------------------*/

static zend_always_inline int RSHUTDOWN_PCS_Loader(TSRMLS_D)
{
	return SUCCESS;
}

/*===============================================================*/
