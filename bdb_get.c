#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* bdb head file */
#include <db.h>   
#define DATABASE "carlos.db"
#define BUFFLEN 64

int main(int argc, char** argv)
{
	char arg_key[BUFFLEN];
	char buf_data[BUFFLEN];
	DB_ENV *db_env;
	DB *dbp;
	DBT key, data, get_data;
	int ret,t_ret;
	u_int32_t env_flags;

	if( argc == 2) {
		strcpy(arg_key, argv[1]);
	}
	else {
		strcpy(arg_key, "key_00001");
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

	if ((ret = db_create(&dbp, db_env, 0)) != 0) {
		fprintf(stderr, "db_create: %s\n", db_strerror(ret));
		exit (1);
	}

	if ((ret = dbp->open(dbp, NULL, DATABASE, NULL, DB_BTREE, DB_CREATE, 0664)) != 0) {
		dbp->err(dbp, ret, "%s", DATABASE);
		exit (1);
	}

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data)); 
	memset(&get_data, 0, sizeof(get_data)); 
	key.data = arg_key;
	key.size = strlen(arg_key);
	get_data.data = buf_data;
	data.size = sizeof(buf_data);
	
	/* get data */
	ret = dbp->get(dbp, NULL, &key, &get_data, 0);
	if (ret == 0) {
		printf("key: %s retrieved, data: %s\n", (char*)key.data, (char*)get_data.data);
	}
	else if(DB_NOTFOUND == ret) {
		printf("key %s NOTFOUND\n", (char*)key.data);
	}
	else {
		dbp->err(dbp, ret, "DB->get");
	}


	dbp->sync(dbp, 0);
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
