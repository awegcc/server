#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* bdb head file */
#include <db.h>   
#define DATABASE "carlos.db"

int main(int argc, char** argv)
{
	DB_ENV *db_env;
	DB *dbp;
	DBC *cur;
	DBT key, get_data;
	int ret,t_ret;
	u_int32_t env_flags, cur_flags;

	memset(&key, 0, sizeof(key));
	memset(&get_data, 0, sizeof(get_data)); 
	if( argc == 2) {
		cur_flags = DB_SET | DB_NEXT;
		key.data = argv[1];
		key.size = strlen(argv[1]);
	}
	else {
		cur_flags = DB_NEXT;
	}

	/* Create an environment object and initialize it for error reporting */
	ret = db_env_create(&db_env, 0);
	if (ret != 0) {
		fprintf(stderr, "Error creating env handle: %s\n", db_strerror(ret));
		return -1;
	}

	/* If the environment does not exist create it. Initialize the in-memory cache. */
	env_flags = DB_CREATE | DB_INIT_MPOOL;
	/* Open the environment. */
	ret = db_env->open(db_env, "/opt/db_env", env_flags, 0);
	if (ret != 0) {
		fprintf(stderr, "Environment open failed: %s", db_strerror(ret));
		return -1;
	}

	ret = db_create(&dbp, db_env, 0);
	if ( ret != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		exit (1);
	}

	ret = dbp->open(dbp, NULL, DATABASE, NULL, DB_BTREE, DB_CREATE, 0664);
	if ( ret != 0) {
		dbp->err(dbp, ret, "%s", DATABASE);
		exit (1);
	}

	ret = dbp->cursor(dbp, NULL, &cur, 0);
	if( 0 != ret ) {
		dbp->err(dbp, ret, "%s", DATABASE);
		exit (1);
	}


	while( (ret = cur->c_get(cur, &key, &get_data, cur_flags)) == 0) {
		printf("key: %s retrieved, data: %s\n", (char*)key.data, (char*)get_data.data);
	}


	/* close, only when the db successful closed,the data can real write to the disk.
	 * if ((t_ret = dbp->close(dbp, 0)) != 0 && ret == 0)
	 * ret = t_ret;
	 * exit(ret);
	 */
	if (dbp != NULL) {
		dbp->close(dbp, 0);
	}
	/* close env
	 * When you are done with an environment, you must close it.
	 * Before you close an environment, make sure you close any opened databases
	 */
	if (db_env != NULL) {
		db_env->close(db_env, 0);
	}

	return 0;
}
