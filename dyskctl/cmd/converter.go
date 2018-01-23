package cmd

import (
	"fmt"
	"path"
	"strings"

	"github.com/khenidak/dysk/pkg/client"
)

type SecretRef struct {
	Name      string  `json:"name"`
	Namespace *string `json:"namespace,omitempty"`
}
type FlexVol struct {
	Driver    string            `json:"driver"`
	FSType    *string           `json:"fsType"`
	ReadOnly  bool              `json:"readOnly"`
	SecretRef *SecretRef        `json:"secretRef"`
	Options   map[string]string `json:"options"`
}
type Metadata struct {
	Name      string             `json:"name"`
	Namespace *string            `json:"namespace,omitempty"`
	Labels    *map[string]string `json:"labels, omitempty"`
}
type Capacity struct {
	Storage string `json:"storage"`
}
type Spec struct {
	AccessModes                   []string  `json:"accessModes"`
	Capacity                      *Capacity `json:"capacity"`
	PersistentVolumeReclaimPolicy *string   `json:"persistentVolumeReclaimPolicy,omitempty"`
	StorageClassName              *string   `json:"storageClassName,omitempty"`
	FlexVolume                    *FlexVol  `json:"flexVolume"`
}
type PersistentVolume struct {
	ApiVersion string `json:"apiVersion"`
	Kind       string `json:"kind"`

	Metadata *Metadata `json:"metadata"`
	Spec     *Spec     `json:"spec"`
}

func dysk2Pv(namespace string, reclaimPolicy string, accessMode string, secretName string,
	secretNamespace string, fsType string, readOnly bool, storageClassName string, labels map[string]string, d *client.Dysk) *PersistentVolume {

	container := path.Dir(d.Path)
	container = container[1:]
	blob := path.Base(d.Path)

	var opts = map[string]string{
		"accountName": d.AccountName,
		"container":   container,
		"blob":        blob,
	}

	secretRef := &SecretRef{
		Name: secretName,
	}

	// Hack broken api for v1.10.+
	if "" != secretNamespace {
		secretRef.Namespace = &secretNamespace
	}
	storageGiB := fmt.Sprintf("%dGi", d.SizeGB)
	pv := &PersistentVolume{
		Metadata: &Metadata{
			Name: strings.ToLower(d.Name),
		},
		Spec: &Spec{
			Capacity: &Capacity{
				Storage: storageGiB,
			},
			FlexVolume: &FlexVol{
				Driver:    "dysk/dysk",
				ReadOnly:  readOnly,
				Options:   opts,
				SecretRef: secretRef,
			},
		},
	}

	if "" != storageClassName {
		pv.Spec.StorageClassName = &storageClassName
	}

	if "" != fsType {
		pv.Spec.FlexVolume.FSType = &fsType
	}

	if 0 < len(labels) {
		pv.Metadata.Labels = &labels
	}

	if "" != namespace {
		pv.Metadata.Namespace = &namespace
	}
	pv.Spec.AccessModes = []string{accessMode}
	pv.Kind = "PersistentVolume"
	pv.ApiVersion = "v1"
	return pv
}
