 find ./nfapi/public_inc/nfapi_interface.h ./nfapi/public_inc/nfapi_nr_interface.h ./nfapi/public_inc/nfapi_nr_interface_scf.h ./sim_common/inc/vendor_ext.h -type f -exec md5sum {} \; | sort -k 2 | md5sum
