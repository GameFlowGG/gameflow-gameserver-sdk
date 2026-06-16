package gameflow

import (
	"encoding/json"
	"testing"
)

func TestParsesCapacityAsStringOrNumber(t *testing.T) {
	var fromStr rawList
	if err := json.Unmarshal([]byte(`{"capacity":"8","values":["a"]}`), &fromStr); err != nil {
		t.Fatal(err)
	}
	if fromStr.Capacity != 8 || len(fromStr.Values) != 1 || fromStr.Values[0] != "a" {
		t.Errorf("from string: got %+v", fromStr)
	}

	var fromNum rawList
	if err := json.Unmarshal([]byte(`{"capacity":8}`), &fromNum); err != nil {
		t.Fatal(err)
	}
	if fromNum.Capacity != 8 {
		t.Errorf("from number: got %d", fromNum.Capacity)
	}

	var empty rawList
	if err := json.Unmarshal([]byte(`{}`), &empty); err != nil {
		t.Fatal(err)
	}
	if empty.Capacity != 0 || len(empty.Values) != 0 {
		t.Errorf("empty: got %+v", empty)
	}
}

func TestAcceptsBothObjectMetaSpellings(t *testing.T) {
	var proto rawGameServer
	if err := json.Unmarshal([]byte(`{"object_meta":{"name":"gs"}}`), &proto); err != nil {
		t.Fatal(err)
	}
	if proto.ObjectMeta == nil || proto.ObjectMeta.Name != "gs" {
		t.Errorf("object_meta: got %+v", proto.ObjectMeta)
	}

	var camel rawGameServer
	if err := json.Unmarshal([]byte(`{"objectMeta":{"name":"gs"}}`), &camel); err != nil {
		t.Fatal(err)
	}
	if camel.ObjectMeta == nil || camel.ObjectMeta.Name != "gs" {
		t.Errorf("objectMeta: got %+v", camel.ObjectMeta)
	}
}

func TestCoercesExplicitNullToDefaults(t *testing.T) {
	var list rawList
	if err := json.Unmarshal([]byte(`{"capacity":null,"values":null}`), &list); err != nil {
		t.Fatal(err)
	}
	if list.Capacity != 0 || len(list.Values) != 0 {
		t.Errorf("null list: got %+v", list)
	}

	var gs rawGameServer
	body := `{"object_meta":{"name":"gs","annotations":null,"labels":null},
		"status":{"ports":null,"lists":null}}`
	if err := json.Unmarshal([]byte(body), &gs); err != nil {
		t.Fatal(err)
	}
	if gs.ObjectMeta == nil || gs.ObjectMeta.Annotations != nil || gs.ObjectMeta.Labels != nil {
		t.Errorf("null meta maps should stay nil: %+v", gs.ObjectMeta)
	}
	if gs.Status == nil || len(gs.Status.Ports) != 0 || len(gs.Status.Lists) != 0 {
		t.Errorf("null status slices/maps should be empty: %+v", gs.Status)
	}
}

func TestIgnoresUnknownFields(t *testing.T) {
	var gs rawGameServer
	body := `{"object_meta":{"name":"gs","resourceVersion":"5"},"extra":true}`
	if err := json.Unmarshal([]byte(body), &gs); err != nil {
		t.Fatal(err)
	}
	if gs.ObjectMeta == nil || gs.ObjectMeta.Name != "gs" {
		t.Errorf("got %+v", gs.ObjectMeta)
	}
}

func TestToInfoDefaultsEmptyCollections(t *testing.T) {
	info := toInfo(&rawGameServer{})
	if info.Ports == nil || info.Labels == nil || info.Annotations == nil {
		t.Errorf("toInfo should default nil collections to empty, got %+v", info)
	}
}
