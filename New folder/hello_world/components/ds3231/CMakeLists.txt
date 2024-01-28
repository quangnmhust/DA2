set(pre_req i2cdev log)
idf_component_register(SRCS "ds3231.c"
                    INCLUDE_DIRS "include"
		    		REQUIRES ${pre_req})
