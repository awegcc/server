#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* bdb head file */
#include <db.h>   
#define DATABASE "carlos.db"
#define BUFFLEN 64
#define COUNT 1*1024*1024



int put_data(char *argkey, char *argvalue)
{
	DB_ENV *db_env;
	DB *dbp;
	DBT key, data;
	int ret,t_ret;
	u_int32_t env_flags = 0;
	u_int32_t put_flags = DB_NOOVERWRITE;

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data)); 

	/* Create an environment object and initialize it for error reporting */
	ret = db_env_create(&db_env, 0);
	if (ret != 0) {
		fprintf(stderr, "Error creating env handle: %s\n", db_strerror(ret));
		return -1;
	}

	/* If the environment does not exist create it. Initialize the in-memory cache. */
	env_flags = DB_CREATE | DB_INIT_MPOOL | DB_INIT_LOG | DB_INIT_LOCK;
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

	key.data = argkey;
	key.size = strlen(argkey);

	data.data = argvalue;
	data.size = strlen(argvalue);

	ret = dbp->put(dbp, NULL, &key, &data, put_flags);
	if (ret == 0) {
		printf("key: %s stored.\n", (char *)key.data);
	}
	else {
		dbp->err(dbp, ret, "DB->put");
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

int main(int argc, char** argv)
{
	long index = 0;
	char buf_key[BUFFLEN];
	char buf_value[BUFFLEN];
	for(index=0; index < COUNT; index++) {
		memset(buf_key, 0, BUFFLEN);
		memset(buf_value, 0, BUFFLEN);
		sprintf(buf_key, "key_%ld", index);
		sprintf(buf_value, "value_%ld", index);
		put_data(buf_key, buf_value);
	}
	
	return 0;
}
